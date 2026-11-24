#include "ui/scenes/scene_youtube.hpp"

#include "input/input_actions.hpp"
#include "main.hpp"
#include "ui/cover_cache.hpp"
#include "ui/widgets/widget_button_icon.hpp"
#include "ui/widgets/widget_sidebar.hpp"
#include "ui/widgets/widget_tooltip.hpp"
#include "utils/app_state.hpp"
#include "utils/display.hpp"
#include "utils/font.hpp"
#include "utils/media_info.hpp"
#include "youtube/youtube.hpp"

#include <atomic>
#include <cstdio>
#include <imgui/imgui.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct PendingPlay {
    bool ready = false;
    std::string title;
    std::string url;
};

struct State {
    char query[128] = "";
    std::vector<youtube::VideoResult> results;
    std::string status;
    std::string error;

    std::mutex mtx;
    std::thread worker;
    std::atomic<bool> busy{false};
    PendingPlay pending_play;
};

State S;

void join_worker() {
    if (S.worker.joinable())
        S.worker.join();
}

template <typename Fn> void launch_task(const char *msg, Fn &&fn) {
    if (S.busy.load())
        return;
    join_worker();
    {
        std::lock_guard<std::mutex> lk(S.mtx);
        S.status = msg;
        S.error.clear();
    }
    S.busy.store(true);
    S.worker = std::thread([fn = std::forward<Fn>(fn)]() {
        fn();
        S.busy.store(false);
    });
}

void start_search() {
    std::string q = S.query;
    launch_task("Recherche sur YouTube...", [q]() {
        std::string err;
        auto res = youtube::search(q, err);
        std::lock_guard<std::mutex> lk(S.mtx);
        S.status.clear();
        if (!err.empty()) {
            S.error = err;
            S.results.clear();
        } else if (res.empty()) {
            S.error = "Aucun resultat pour \"" + q + "\"";
            S.results.clear();
        } else {
            S.results = std::move(res);
        }
    });
}

void play_video(const youtube::VideoResult &v) {
    launch_task("Chargement du flux YouTube...", [v]() {
        std::string url, err;
        if (youtube::resolve_stream(v.id, url, err)) {
            std::lock_guard<std::mutex> lk(S.mtx);
            S.status.clear();
            S.pending_play = {true, v.title, url};
        } else {
            std::lock_guard<std::mutex> lk(S.mtx);
            S.status.clear();
            S.error = err;
        }
    });
}

bool video_card(const char *id, const youtube::VideoResult &v, float w, float h_img) {
    ImGui::PushID(id);
    float total_h = h_img + 68.0f;
    bool clicked = ImGui::InvisibleButton("##ytcard", ImVec2(w, total_h));
    bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    ImVec2 img_p1(p0.x + w, p0.y + h_img);

    ImU32 card_bg = hovered ? IM_COL32(42, 20, 26, 255) : IM_COL32(20, 14, 17, 255);
    dl->AddRectFilled(p0, p1, card_bg, 10.0f);

    ImTextureID tex = ui_covers::get(v.thumb);
    if (tex) {
        dl->AddImageRounded(tex, p0, img_p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), 10.0f, ImDrawFlags_RoundCornersTop);
    } else {
        dl->AddRectFilledMultiColor(p0, img_p1, IM_COL32(48, 18, 24, 255), IM_COL32(28, 12, 16, 255), IM_COL32(14, 10, 12, 255), IM_COL32(24, 14, 18, 255));
        ImFont *lf = font_lucide();
        ImVec2 isz = lf->CalcTextSizeA(32.0f, FLT_MAX, 0.0f, ICON_YOUTUBE);
        dl->AddText(lf, 32.0f, ImVec2(p0.x + (w - isz.x) * 0.5f, p0.y + (h_img - isz.y) * 0.5f), IM_COL32(220, 60, 75, 200), ICON_YOUTUBE);
    }

    if (!v.duration.empty()) {
        ImVec2 dsz = ImGui::CalcTextSize(v.duration.c_str());
        ImVec2 bp1(img_p1.x - 6.0f, img_p1.y - 6.0f);
        ImVec2 bp0(bp1.x - dsz.x - 10.0f, bp1.y - dsz.y - 4.0f);
        dl->AddRectFilled(bp0, bp1, IM_COL32(0, 0, 0, 200), 4.0f);
        dl->AddText(ImVec2(bp0.x + 5.0f, bp0.y + 2.0f), IM_COL32(255, 255, 255, 255), v.duration.c_str());
    }

    if (hovered) {
        dl->AddRect(p0, p1, IM_COL32(239, 68, 68, 255), 10.0f, 0, 3.0f);
    } else {
        dl->AddRect(p0, p1, IM_COL32(65, 30, 38, 160), 10.0f, 0, 1.0f);
    }

    ImVec2 txt_min(p0.x + 8.0f, img_p1.y + 6.0f);
    ImVec2 txt_max(p1.x - 8.0f, p1.y - 6.0f);
    dl->PushClipRect(txt_min, txt_max, true);
    dl->AddText(nullptr, 0.0f, txt_min, IM_COL32(245, 240, 242, 255), v.title.c_str(), nullptr, w - 16.0f);
    std::string sub = v.channel;
    if (!v.views.empty())
        sub += (sub.empty() ? "" : " - ") + v.views;
    if (!sub.empty())
        dl->AddText(nullptr, 0.0f, ImVec2(txt_min.x, img_p1.y + 38.0f), IM_COL32(170, 145, 152, 255), sub.c_str(), nullptr, w - 16.0f);
    dl->PopClipRect();

    ImGui::PopID();
    return clicked;
}

} // namespace

void scene_youtube_input() {
    if (input_pressed(BUTTON_B) && !widget_sidebar_is_open()) {
        widget_sidebar_set_open(true);
    }
}

void scene_youtube_render() {
    {
        PendingPlay play;
        {
            std::lock_guard<std::mutex> lk(S.mtx);
            if (S.pending_play.ready) {
                play = std::move(S.pending_play);
                S.pending_play.ready = false;
            }
        }
        if (play.ready) {
            auto new_info = std::make_unique<media_info>();
            media_info_set(std::move(new_info));
            media_info *mi = media_info_get();
            mi->type = 'V';
            mi->path = play.url;
            mi->referer = "https://www.youtube.com/";
            mi->filename = play.title;
            mi->return_state = STATE_YOUTUBE;
            mi->current_playback_time = 0;
            mi->total_playback_time = 0;
            mi->current_audio_track_id = 1;
            mi->total_audio_track_count = 1;
            mi->current_caption_id = 1;
            mi->total_caption_count = 1;
            app_state_set(STATE_PLAYING_VIDEO);
            return;
        }
    }

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

    if (ImGui::Begin(VERSION_STRING, nullptr, window_flags)) {
        ImGui::SetWindowPos(ImVec2(0, 0));
        ImGui::SetWindowSize(ImVec2(display_get().width, display_get().height - TOOLTIP_BAR_HEIGHT));

        widget_sidebar_begin_layout();

        ImGui::BeginChild("YouTubeContent", ImVec2(0, 0), true, ImGuiWindowFlags_None);

        widget_sidebar_toggle_button();
        ImGui::SameLine();
        ImGui::Text("YouTube");
        ImGui::Separator();

        bool busy = S.busy.load();
        std::string status, error;
        std::vector<youtube::VideoResult> results;
        {
            std::lock_guard<std::mutex> lk(S.mtx);
            status = S.status;
            error = S.error;
            results = S.results;
        }

        ImGui::BeginDisabled(busy);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20.0f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 200.0f);
        bool submit = ImGui::InputTextWithHint("##yt_query", "Rechercher une video YouTube...", S.query, sizeof(S.query), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (widget_lucide_button("yt_search", ICON_SEARCH, "Rechercher", true, ImVec2(180.0f, 44.0f)) || submit) {
            start_search();
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();

        if (!status.empty())
            ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.30f, 1.0f), "%s", status.c_str());
        if (!error.empty())
            ImGui::TextColored(ImVec4(0.98f, 0.32f, 0.35f, 1.0f), "%s", error.c_str());

        ImGui::Spacing();

        if (results.empty() && status.empty() && error.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Tape un titre ci-dessus pour rechercher sur YouTube.");
        } else if (!results.empty()) {
            const int cols = 3;
            float avail = ImGui::GetContentRegionAvail().x;
            float gap = 16.0f;
            float card_w = (avail - gap * (cols - 1) - 16.0f) / cols;
            float card_img_h = card_w * 0.5625f; // 16:9

            ImGui::BeginDisabled(busy);
            for (size_t i = 0; i < results.size(); i++) {
                if (i % cols != 0)
                    ImGui::SameLine(0.0f, gap);
                char id[32];
                snprintf(id, sizeof(id), "yt_%zu", i);
                if (video_card(id, results[i], card_w, card_img_h)) {
                    play_video(results[i]);
                }
            }
            ImGui::EndDisabled();
        }

        ImGui::EndChild();
        widget_sidebar_end_layout();
        ImGui::End();
    }

    widget_tooltip_render();
}

void scene_youtube_shutdown() { join_worker(); }

