#ifndef UI_WIDGET_BUTTON_ICON_H
#define UI_WIDGET_BUTTON_ICON_H

#include "utils/font.hpp"

#include <imgui/imgui.h>

static bool widget_button_icon(const char *label, const char *icon, bool selected, ImVec2 size) {
    ImGui::PushID(label);

    bool clicked = ImGui::Button("##sidebar_btn", size);

    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    ImDrawList *dl = ImGui::GetWindowDrawList();

    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();

    float icon_size = size.y;

    ImU32 bg = ImGui::GetColorU32(ImGuiCol_Button);

    if (selected)
        bg = ImGui::GetColorU32(ImGuiCol_Header);
    else if (hovered)
        bg = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
    else if (active)
        bg = ImGui::GetColorU32(ImGuiCol_ButtonActive);

    dl->AddRectFilled(p0, p1, bg);

    ImVec2 icon_min = p0;
    ImVec2 icon_max = ImVec2(p0.x + icon_size, p0.y + icon_size);

    dl->AddRectFilled(icon_min, icon_max, ImGui::GetColorU32(ImGuiCol_FrameBg));
    dl->AddLine(ImVec2(p0.x + icon_size, p0.y), ImVec2(p0.x + icon_size, p1.y), ImGui::GetColorU32(ImGuiCol_Border));

    if (icon) {
        ImFont *lf = font_lucide();
        float font_sz = 28.0f;
        ImVec2 text_size = lf->CalcTextSizeA(font_sz, FLT_MAX, 0.0f, icon);
        ImVec2 icon_pos(icon_min.x + (icon_size - text_size.x) * 0.5f, icon_min.y + (icon_size - text_size.y) * 0.5f);

        dl->AddText(lf, font_sz, icon_pos, ImGui::GetColorU32(ImGuiCol_Text), icon);
    } else {
        dl->AddText(ImVec2(icon_min.x + 20, icon_min.y + 20), IM_COL32(255, 0, 0, 255), "?");
    }

    float text_x = p0.x + icon_size + 12.0f;
    float text_y = p0.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f;

    dl->AddText(ImVec2(text_x, text_y), ImGui::GetColorU32(ImGuiCol_Text), label);

    ImGui::PopID();

    return clicked;
}

// Compact button with a Lucide icon on the left and text on the right.
static bool widget_lucide_button(const char *id, const char *icon, const char *label, bool active = false, ImVec2 size = ImVec2(0, 42)) {
    ImGui::PushID(id);
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f, 0.15f, 0.23f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.94f, 0.24f, 0.30f, 1.00f));
    }

    float font_sz = 22.0f;
    ImFont *lf = font_lucide();
    float icon_w = (icon && *icon) ? lf->CalcTextSizeA(font_sz, FLT_MAX, 0.0f, icon).x : 0.0f;
    float label_w = (label && *label) ? ImGui::CalcTextSize(label).x : 0.0f;
    float pad_x = ImGui::GetStyle().FramePadding.x;
    float gap = (icon_w > 0 && label_w > 0) ? 10.0f : 0.0f;
    ImVec2 actual_size = size;
    if (actual_size.x <= 0.0f)
        actual_size.x = pad_x * 2.0f + icon_w + gap + label_w;

    bool clicked = ImGui::Button("##lbtn", actual_size);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    float w = p1.x - p0.x;
    float h = p1.y - p0.y;
    float content_w = icon_w + gap + label_w;
    float x = p0.x + (w - content_w) * 0.5f;

    ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    if (icon && *icon) {
        ImVec2 isz = lf->CalcTextSizeA(font_sz, FLT_MAX, 0.0f, icon);
        dl->AddText(lf, font_sz, ImVec2(x, p0.y + (h - isz.y) * 0.5f), col, icon);
        x += icon_w + gap;
    }
    if (label && *label) {
        dl->AddText(ImVec2(x, p0.y + (h - ImGui::GetTextLineHeight()) * 0.5f), col, label);
    }

    if (active)
        ImGui::PopStyleColor(2);
    ImGui::PopID();
    return clicked;
}

#endif
