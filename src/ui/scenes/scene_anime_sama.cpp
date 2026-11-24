#include "ui/scenes/scene_anime_sama.hpp"

#include "animesama/animesama.hpp"
#include "input/input_actions.hpp"
#include "logger/logger.hpp"
#include "main.hpp"
#include "ui/cover_cache.hpp"
#include "ui/widgets/widget_sidebar.hpp"
#include "ui/widgets/widget_tooltip.hpp"
#include "utils/app_state.hpp"
#include "utils/display.hpp"
#include "utils/font.hpp"
#include "utils/media_info.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <imgui/imgui.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

enum class Page { Search, Detail };

struct PendingPlay {
    bool ready = false;
    std::string title;
    animesama::Stream stream;
};

struct State {
    Page page = Page::Search;
    char query[128] = "";
    animesama::Lang lang = animesama::Lang::VOSTFR;

    std::vector<animesama::SearchResult> results;
    animesama::SearchResult selected_anime;
    animesama::AnimeInfo info;
    int selected_season_idx = 0;
    animesama::EpisodeList episodes;

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

template <typename Fn> void launch_task(const char *status_msg, Fn &&fn) {
    if (S.busy.load())
        return;
    join_worker();
    {
        std::lock_guard<std::mutex> lk(S.mtx);
        S.status = status_msg;
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
    launch_task("Recherche sur Anime-Sama...", [q]() {
        std::string err;
        auto res = animesama::search(q, err);
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

void load_season_episodes(const animesama::Season &season, animesama::Lang lang) {
    S.lang = lang;
    launch_task("Chargement des episodes...", [season, lang]() {
        animesama::EpisodeList list;
        std::string err;
        if (animesama::get_episodes(season, lang, list, err)) {
            std::lock_guard<std::mutex> lk(S.mtx);
            S.status.clear();
            S.episodes = std::move(list);
        } else {
            std::lock_guard<std::mutex> lk(S.mtx);
            S.status.clear();
            S.episodes = animesama::EpisodeList{};
            S.error = err;
        }
    });
}

void open_anime(const animesama::SearchResult &item) {
    S.selected_anime = item;
    animesama::Lang lang = S.lang;
    launch_task("Chargement de la fiche...", [item, lang]() {
        animesama::AnimeInfo info;
        std::string err;
        if (!animesama::get_info(item, info, err)) {
            std::lock_guard<std::mutex> lk(S.mtx);
            S.status.clear();
            S.error = err;
            return;
        }
        animesama::EpisodeList list;
        std::string ep_err;
        animesama::get_episodes(info.seasons[0], lang, list, ep_err);

        std::lock_guard<std::mutex> lk(S.mtx);
        S.status.clear();
        S.info = std::move(info);
        S.selected_season_idx = 0;
        S.episodes = std::move(list);
        S.error = ep_err;
        S.page = Page::Detail;
    });
}

void play_episode(const animesama::Episode &ep) {
    animesama::EpisodeList list_copy;
    std::string title;
    {
        std::lock_guard<std::mutex> lk(S.mtx);
        list_copy = S.episodes;
        title = S.info.title + " - Ep " + std::to_string(ep.number);
    }
    launch_task("Resolution du flux video...", [list_copy, ep, title]() {
        animesama::Stream st;
        std::string err;
        if (animesama::resolve_episode(list_copy, ep, st, err)) {
            std::lock_guard<std::mutex> lk(S.mtx);
            S.status.clear();
            S.pending_play = {true, title, st};
        } else {
            std::lock_guard<std::mutex> lk(S.mtx);
            S.status.clear();
            S.error = err;
        }
    });
}

void go_back() {
    if (S.busy.load())
        return;
    std::lock_guard<std::mutex> lk(S.mtx);
    S.error.clear();
    if (S.page == Page::Detail)
        S.page = Page::Search;
}

// CoffeeFlix-style poster card: cover art on top, focus ring, title below.
bool poster_card(const char *id, const std::string &title, const std::string &cover_url, float w, float h_img) {
    ImGui::PushID(id);
    float total_h = h_img + 56.0f;
    bool clicked = ImGui::InvisibleButton("##card", ImVec2(w, total_h));
    bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    ImVec2 img_p1(p0.x + w, p0.y + h_img);

    ImU32 card_bg = hovered ? IM_COL32(34, 40, 54, 255) : IM_COL32(22, 26, 36, 255);
    dl->AddRectFilled(p0, p1, card_bg, 10.0f);

    ImTextureID tex = ui_covers::get(cover_url);
    if (tex) {
        dl->AddImageRounded(tex, p0, img_p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), 10.0f, ImDrawFlags_RoundCornersTop);
    } else {
        dl->AddRectFilledMultiColor(p0, img_p1, IM_COL32(35, 45, 70, 255), IM_COL32(25, 32, 50, 255), IM_COL32(18, 22, 34, 255), IM_COL32(25, 30, 45, 255));
        ImVec2 ic_sz = ImGui::CalcTextSize(ICON_ANIME);
        dl->AddText(ImVec2(p0.x + (w - ic_sz.x) * 0.5f, p0.y + (h_img - ic_sz.y) * 0.5f), IM_COL32(120, 140, 180, 180), ICON_ANIME);
    }

    if (hovered) {
        dl->AddRect(p0, p1, IM_COL32(96, 165, 250, 255), 10.0f, 0, 3.0f);
    } else {
        dl->AddRect(p0, p1, IM_COL32(45, 52, 68, 160), 10.0f, 0, 1.0f);
    }

    ImVec2 txt_min(p0.x + 8.0f, img_p1.y + 6.0f);
    ImVec2 txt_max(p1.x - 8.0f, p1.y - 6.0f);
    dl->PushClipRect(txt_min, txt_max, true);
    dl->AddText(nullptr, 0.0f, txt_min, IM_COL32(240, 244, 250, 255), title.c_str(), nullptr, w - 16.0f);
    dl->PopClipRect();

    ImGui::PopID();
    return clicked;
}

bool pill_button(const char *label, bool active, ImVec2 size = ImVec2(0, 42)) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.23f, 0.51f, 0.96f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.58f, 0.98f, 1.00f));
    }
    bool clicked = ImGui::Button(label, size);
    if (active)
        ImGui::PopStyleColor(2);
    return clicked;
}

} // namespace

void scene_anime_sama_input() {
    if (input_pressed(BUTTON_B)) {
        go_back();
    }
}

void scene_anime_sama_render() {
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
            mi->path = play.stream.url;
            mi->referer = play.stream.referer;
            mi->filename = play.title;
            mi->return_state = STATE_ANIME_SAMA;
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

        ImGui::Columns(2, nullptr, false);
        ImGui::SetColumnWidth(0, WIDGET_SIDEBAR_WIDTH);
        widget_sidebar_render();
        ImGui::NextColumn();

        ImGui::BeginChild("AnimeSamaContent", ImVec2(0, 0), true, ImGuiWindowFlags_None);

        bool busy = S.busy.load();
        Page page;
        std::string status, error;
        {
            std::lock_guard<std::mutex> lk(S.mtx);
            page = S.page;
            status = S.status;
            error = S.error;
        }

        if (page == Page::Search) {
            ImGui::Text("Anime-Sama");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 160.0f);
            ImGui::BeginDisabled(busy);
            if (pill_button(S.lang == animesama::Lang::VOSTFR ? "VOSTFR" : "VF", true, ImVec2(140.0f, 38.0f))) {
                S.lang = (S.lang == animesama::Lang::VOSTFR) ? animesama::Lang::VF : animesama::Lang::VOSTFR;
            }
            ImGui::EndDisabled();

            ImGui::Separator();

            ImGui::BeginDisabled(busy);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20.0f);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 200.0f);
            bool submit = ImGui::InputTextWithHint("##anime_query", "Rechercher un anime (ex: Frieren, One Piece)...", S.query, sizeof(S.query), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (pill_button("Rechercher", true, ImVec2(180.0f, 44.0f)) || submit) {
                start_search();
            }
            ImGui::PopStyleVar();
            ImGui::EndDisabled();

            if (!status.empty())
                ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.30f, 1.0f), "%s", status.c_str());
            if (!error.empty())
                ImGui::TextColored(ImVec4(0.98f, 0.38f, 0.38f, 1.0f), "%s", error.c_str());

            ImGui::Spacing();

            std::vector<animesama::SearchResult> results;
            {
                std::lock_guard<std::mutex> lk(S.mtx);
                results = S.results;
            }

            if (results.empty() && status.empty() && error.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("Tape le nom d'un anime ci-dessus pour parcourir le catalogue.");
            } else if (!results.empty()) {
                const int cols = 4;
                float avail = ImGui::GetContentRegionAvail().x;
                float gap = 16.0f;
                float card_w = (avail - gap * (cols - 1) - 16.0f) / cols;
                float card_img_h = card_w * 1.38f;

                ImGui::BeginDisabled(busy);
                for (size_t i = 0; i < results.size(); i++) {
                    if (i % cols != 0)
                        ImGui::SameLine(0.0f, gap);
                    char id[32];
                    snprintf(id, sizeof(id), "res_%zu", i);
                    if (poster_card(id, results[i].title, results[i].cover, card_w, card_img_h)) {
                        open_anime(results[i]);
                    }
                }
                ImGui::EndDisabled();
            }
        } else if (page == Page::Detail) {
            animesama::SearchResult sel;
            animesama::AnimeInfo info;
            int season_idx = 0;
            std::vector<animesama::Episode> eps;
            {
                std::lock_guard<std::mutex> lk(S.mtx);
                sel = S.selected_anime;
                info = S.info;
                season_idx = S.selected_season_idx;
                eps = S.episodes.episodes;
            }

            ImGui::BeginDisabled(busy);
            if (pill_button("< Retour", false, ImVec2(140.0f, 40.0f)))
                go_back();
            ImGui::SameLine();
            bool is_vostfr = (S.lang == animesama::Lang::VOSTFR);
            if (pill_button("VOSTFR", is_vostfr, ImVec2(120.0f, 40.0f)) && !is_vostfr && season_idx < (int)info.seasons.size()) {
                load_season_episodes(info.seasons[season_idx], animesama::Lang::VOSTFR);
            }
            ImGui::SameLine();
            if (pill_button("VF", !is_vostfr, ImVec2(90.0f, 40.0f)) && is_vostfr && season_idx < (int)info.seasons.size()) {
                load_season_episodes(info.seasons[season_idx], animesama::Lang::VF);
            }
            ImGui::EndDisabled();

            ImGui::Spacing();

            float poster_w = 180.0f;
            float poster_h = 252.0f;
            ImVec2 hp0 = ImGui::GetCursorScreenPos();
            ImTextureID cover_tex = ui_covers::get(sel.cover);
            ImDrawList *dl = ImGui::GetWindowDrawList();
            if (cover_tex) {
                dl->AddImageRounded(cover_tex, hp0, ImVec2(hp0.x + poster_w, hp0.y + poster_h), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), 10.0f);
            } else {
                dl->AddRectFilled(hp0, ImVec2(hp0.x + poster_w, hp0.y + poster_h), IM_COL32(28, 34, 48, 255), 10.0f);
            }

            ImGui::Dummy(ImVec2(poster_w, poster_h));
            ImGui::SameLine(0.0f, 20.0f);

            ImGui::BeginGroup();
            ImGui::TextWrapped("%s", info.title.c_str());
            if (!info.alt_titles.empty()) {
                ImGui::TextDisabled("%s", info.alt_titles[0].c_str());
            }
            ImGui::Spacing();
            if (!info.synopsis.empty()) {
                ImGui::BeginChild("SynopsisBox", ImVec2(0, 170.0f), false);
                ImGui::TextWrapped("%s", info.synopsis.c_str());
                ImGui::EndChild();
            }
            ImGui::EndGroup();

            ImGui::Spacing();
            ImGui::Separator();

            ImGui::BeginDisabled(busy);
            for (size_t i = 0; i < info.seasons.size(); i++) {
                if (i > 0)
                    ImGui::SameLine();
                ImGui::PushID((int)i);
                if (pill_button(info.seasons[i].name.c_str(), (int)i == season_idx, ImVec2(0, 40.0f))) {
                    {
                        std::lock_guard<std::mutex> lk(S.mtx);
                        S.selected_season_idx = (int)i;
                    }
                    load_season_episodes(info.seasons[i], S.lang);
                }
                ImGui::PopID();
            }
            ImGui::EndDisabled();

            if (!status.empty())
                ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.30f, 1.0f), "%s", status.c_str());
            if (!error.empty())
                ImGui::TextColored(ImVec4(0.98f, 0.38f, 0.38f, 1.0f), "%s", error.c_str());

            ImGui::Spacing();

            if (!eps.empty()) {
                const int cols = 4;
                float avail = ImGui::GetContentRegionAvail().x;
                float gap = 12.0f;
                float btn_w = (avail - gap * (cols - 1) - 16.0f) / cols;

                ImGui::BeginDisabled(busy);
                for (size_t i = 0; i < eps.size(); i++) {
                    if (i % cols != 0)
                        ImGui::SameLine(0.0f, gap);
                    char label[48];
                    snprintf(label, sizeof(label), "%s  Episode %d", ICON_PLAY, eps[i].number);
                    ImGui::PushID((int)i);
                    if (ImGui::Button(label, ImVec2(btn_w, 52.0f))) {
                        play_episode(eps[i]);
                    }
                    ImGui::PopID();
                }
                ImGui::EndDisabled();
            }
        }

        ImGui::EndChild();
        ImGui::Columns(1);
        ImGui::End();
    }

    widget_tooltip_render();
}

void scene_anime_sama_shutdown() { join_worker(); }
