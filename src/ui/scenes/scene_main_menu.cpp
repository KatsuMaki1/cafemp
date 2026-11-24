#include "scene_main_menu.hpp"

#include "main.hpp"
#include "ui/widgets/widget_sidebar.hpp"
#include "ui/widgets/widget_tooltip.hpp"
#include "utils/display.hpp"

#include <imgui/imgui.h>

void scene_main_menu_render() {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(display_get().width, display_get().height - TOOLTIP_BAR_HEIGHT), ImGuiCond_Always);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

    if (ImGui::Begin(VERSION_STRING, nullptr, window_flags)) {
        widget_sidebar_begin_layout();

        if (ImGui::BeginChild("Content", ImVec2(0, 0), true)) {
            widget_sidebar_toggle_button();
            ImGui::SameLine();
            ImGui::Text("Welcome to %s!", VERSION_STRING);
            ImGui::Separator();

            ImGui::BeginChild("changelog", ImVec2(0, 512), true);

            ImGui::TextUnformatted("What's New");
            ImGui::Spacing();

            ImGui::BulletText("Anime-Sama streaming (VOSTFR / VF, HLS & MP4)");
            ImGui::BulletText("YouTube search & playback (InnerTube)");
            ImGui::BulletText("Hardware video decoding for H.264 (up to 1080p60)");
            ImGui::BulletText("General stability improvements");
            ImGui::BulletText("PDF and EPUB reader");
            ImGui::BulletText("Wiimote support");
            ImGui::BulletText("Pro controller support");
            ImGui::BulletText("Switch between multiple audio tracks");
            ImGui::BulletText("User interface improvements");
            ImGui::BulletText("Display embedded cover art for audio files (folder.jpg)");
            ImGui::BulletText("USB drive support (MBR/FAT32)");
            ImGui::EndChild();
        }

        ImGui::EndChild();
        widget_sidebar_end_layout();
    }

    ImGui::End();

    widget_tooltip_render();
}
