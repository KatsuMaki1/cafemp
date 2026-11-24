#include "ui/scenes/scene_anime_sama.hpp"

#include "animesama/animesama.hpp"
#include "input/input_actions.hpp"
#include "logger/logger.hpp"
#include "main.hpp"
#include "ui/widgets/widget_button_icon.hpp"
#include "ui/widgets/widget_sidebar.hpp"
#include "ui/widgets/widget_tooltip.hpp"
#include "utils/app_state.hpp"
#include "utils/display.hpp"
#include "utils/font.hpp"
#include "utils/media_info.hpp"

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

enum class Page { Search, Info, Episodes };

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
    animesama::Season selected_season;
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

void open_anime(const animesama::SearchResult &item) {
    S.selected_anime = item;
    launch_task("Chargement des saisons...", [item]() {
        animesama::AnimeInfo info;
        std::string err;
        if (animesama::get_info(item, info, err)) {
            std::lock_guard<std::mutex> lk(S.mtx);
            S.status.clear();
            S.info = std::move(info);
            S.page = Page::Info;
        } else {
            std::lock_guard<std::mutex> lk(S.mtx);
            S.status.clear();
            S.error = err;
        }
    });
}

void open_season(const animesama::Season &season, animesama::Lang lang) {
    S.selected_season = season;
    S.lang = lang;
    launch_task("Chargement des episodes...", [season, lang]() {
        animesama::EpisodeList list;
        std::string err;
        if (animesama::get_episodes(season, lang, list, err)) {
            std::lock_guard<std::mutex> lk(S.mtx);
            S.status.clear();
            S.episodes = std::move(list);
            S.page = Page::Episodes;
        } else {
            std::lock_guard<std::mutex> lk(S.mtx);
            S.status.clear();
            S.error = err;
        }
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
    if (S.page == Page::Episodes)
        S.page = Page::Info;
    else if (S.page == Page::Info)
        S.page = Page::Search;
}

} // namespace

void scene_anime_sama_input() {
    if (input_pressed(BUTTON_B)) {
        go_back();
    }
}

void scene_anime_sama_render() {
    // Check if a background episode resolution completed and start playback on the UI thread.
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
            ImGui::Separator();

            ImGui::BeginDisabled(busy);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 200.0f);
            bool submit = ImGui::InputText("##anime_query", S.query, sizeof(S.query), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Rechercher", ImVec2(180.0f, 0)) || submit) {
                start_search();
            }
            ImGui::EndDisabled();

            if (!status.empty())
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "%s", status.c_str());
            if (!error.empty())
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", error.c_str());

            ImGui::Spacing();

            std::vector<animesama::SearchResult> results;
            {
                std::lock_guard<std::mutex> lk(S.mtx);
                results = S.results;
            }
            for (size_t i = 0; i < results.size(); i++) {
                ImGui::PushID((int)i);
                ImGui::BeginDisabled(busy);
                if (widget_button_icon(results[i].title.c_str(), ICON_ANIME, false, ImVec2(-1, 64))) {
                    open_anime(results[i]);
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        } else if (page == Page::Info) {
            animesama::AnimeInfo info;
            {
                std::lock_guard<std::mutex> lk(S.mtx);
                info = S.info;
            }

            ImGui::BeginDisabled(busy);
            if (ImGui::Button("< Retour", ImVec2(140.0f, 44.0f)))
                go_back();
            ImGui::SameLine();
            if (ImGui::Button(S.lang == animesama::Lang::VOSTFR ? "Langue: VOSTFR" : "Langue: VF", ImVec2(220.0f, 44.0f))) {
                S.lang = (S.lang == animesama::Lang::VOSTFR) ? animesama::Lang::VF : animesama::Lang::VOSTFR;
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::Text("%s", info.title.c_str());
            ImGui::Separator();

            if (!info.synopsis.empty()) {
                ImGui::TextWrapped("%s", info.synopsis.c_str());
                ImGui::Separator();
            }

            if (!status.empty())
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "%s", status.c_str());
            if (!error.empty())
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", error.c_str());

            for (size_t i = 0; i < info.seasons.size(); i++) {
                ImGui::PushID((int)i);
                ImGui::BeginDisabled(busy);
                if (widget_button_icon(info.seasons[i].name.c_str(), ICON_FOLDER, false, ImVec2(-1, 64))) {
                    open_season(info.seasons[i], S.lang);
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        } else if (page == Page::Episodes) {
            animesama::AnimeInfo info;
            animesama::Season season;
            std::vector<animesama::Episode> eps;
            {
                std::lock_guard<std::mutex> lk(S.mtx);
                info = S.info;
                season = S.selected_season;
                eps = S.episodes.episodes;
            }

            ImGui::BeginDisabled(busy);
            if (ImGui::Button("< Retour", ImVec2(140.0f, 44.0f)))
                go_back();
            ImGui::SameLine();
            if (ImGui::Button(S.lang == animesama::Lang::VOSTFR ? "VOSTFR" : "VF", ImVec2(140.0f, 44.0f))) {
                animesama::Lang next = (S.lang == animesama::Lang::VOSTFR) ? animesama::Lang::VF : animesama::Lang::VOSTFR;
                open_season(season, next);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::Text("%s - %s", info.title.c_str(), season.name.c_str());
            ImGui::Separator();

            if (!status.empty())
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "%s", status.c_str());
            if (!error.empty())
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", error.c_str());

            for (size_t i = 0; i < eps.size(); i++) {
                char label[64];
                snprintf(label, sizeof(label), "Episode %d", eps[i].number);
                ImGui::PushID((int)i);
                ImGui::BeginDisabled(busy);
                if (widget_button_icon(label, ICON_PLAY, false, ImVec2(-1, 64))) {
                    play_episode(eps[i]);
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }

        ImGui::EndChild();
        ImGui::Columns(1);
        ImGui::End();
    }

    widget_tooltip_render();
}

void scene_anime_sama_shutdown() { join_worker(); }
