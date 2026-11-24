#include "ui/widgets/widget_sidebar.hpp"

#include "main.hpp"
#include "ui/scenes/scene_file_browser.hpp"
#include "ui/widgets/widget_button_icon.hpp"
#include "utils/app_state.hpp"
#include "utils/font.hpp"
#include "utils/usb.hpp"

#include <algorithm>
#include <imgui/imgui.h>

static bool g_sidebar_open = true;
static float g_sidebar_anim = 1.0f; // 0.0 = collapsed, 1.0 = open

bool widget_sidebar_is_open() { return g_sidebar_open; }

void widget_sidebar_set_open(bool open) { g_sidebar_open = open; }

void widget_sidebar_toggle() { g_sidebar_open = !g_sidebar_open; }

float widget_sidebar_current_width() {
    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f || dt > 0.1f)
        dt = 1.0f / 60.0f;
    float target = g_sidebar_open ? 1.0f : 0.0f;
    float speed = 12.0f;
    g_sidebar_anim += (target - g_sidebar_anim) * std::min(1.0f, dt * speed);
    if (g_sidebar_anim < 0.005f && !g_sidebar_open)
        g_sidebar_anim = 0.0f;
    if (g_sidebar_anim > 0.995f && g_sidebar_open)
        g_sidebar_anim = 1.0f;
    return WIDGET_SIDEBAR_WIDTH * g_sidebar_anim;
}

static void render_sidebar_width(float w) {
    ImVec2 size(std::max(1.0f, w - 16.0f), 56.0f);

    ImGui::BeginChild("Sidebar", ImVec2(w, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar);

    if (w > 120.0f) {
        if (widget_button_icon("Home", ICON_HOME, app_state_get() == STATE_MENU, size)) {
            app_state_set(STATE_MENU);
        }
        if (widget_button_icon("Video", ICON_VIDEO, app_state_get() == STATE_MENU_FILES, size)) {
            app_state_set(STATE_MENU_FILES);
            scene_file_browser_cd(MEDIA_PATH_VIDEO);
        }
        if (widget_button_icon("Audio", ICON_AUDIO, app_state_get() == STATE_MENU_FILES, size)) {
            app_state_set(STATE_MENU_FILES);
            scene_file_browser_cd(MEDIA_PATH_AUDIO);
        }
        if (widget_button_icon("Photo", ICON_PHOTO, app_state_get() == STATE_MENU_FILES, size)) {
            app_state_set(STATE_MENU_FILES);
            scene_file_browser_cd(MEDIA_PATH_PHOTO);
        }
        if (widget_button_icon("Library", ICON_LIBRARY, app_state_get() == STATE_MENU_FILES, size)) {
            app_state_set(STATE_MENU_FILES);
            scene_file_browser_cd(MEDIA_PATH_PDF);
        }

#ifndef PLATFORM_WIIU_LEGACY
        if (usb_active_drive()) {
            if (widget_button_icon("USB Drive", ICON_USB, app_state_get() == STATE_MENU_FILES, size)) {
                app_state_set(STATE_MENU_FILES);
                scene_file_browser_cd(MEDIA_PATH_USB);
            }
        }
#endif

        if (widget_button_icon("Anime-Sama", ICON_ANIME, app_state_get() == STATE_ANIME_SAMA, size)) {
            app_state_set(STATE_ANIME_SAMA);
        }
        if (widget_button_icon("YouTube", ICON_YOUTUBE, app_state_get() == STATE_YOUTUBE, size)) {
            app_state_set(STATE_YOUTUBE);
        }
    }

    ImGui::EndChild();
}

void widget_sidebar_begin_layout() {
    float w = widget_sidebar_current_width();
    if (w > 2.0f) {
        ImGui::Columns(2, "MainLayoutCols", false);
        ImGui::SetColumnWidth(0, w + 12.0f);
        render_sidebar_width(w);
        ImGui::NextColumn();
    }
}

void widget_sidebar_end_layout() {
    if (g_sidebar_anim > 0.01f && WIDGET_SIDEBAR_WIDTH * g_sidebar_anim > 2.0f) {
        ImGui::Columns(1);
    }
}

void widget_sidebar_toggle_button() {
    const char *ic = g_sidebar_open ? ICON_PANEL_CLOSE : ICON_PANEL_OPEN;
    if (widget_lucide_button("sidebar_toggle", ic, "Menu", g_sidebar_open, ImVec2(120.0f, 38.0f))) {
        widget_sidebar_toggle();
    }
}

void widget_sidebar_render() { render_sidebar_width(WIDGET_SIDEBAR_WIDTH); }
