#include "app/theme.h"

#include <imgui.h>

namespace de::app {

void applyTheme() {
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.WindowRounding = s.ChildRounding = s.FrameRounding = s.PopupRounding = s.ScrollbarRounding = s.GrabRounding = 0;
    s.TabRounding = 0;
    s.WindowBorderSize = 1;
    s.FrameBorderSize = 1;
    s.WindowPadding = {6, 6};
    s.FramePadding = {6, 3};
    s.ItemSpacing = {6, 4};
    s.ItemInnerSpacing = {4, 4};
    s.IndentSpacing = 14;
    s.ScrollbarSize = 12;
    s.GrabMinSize = 8;
    s.WindowTitleAlign = {0, 0.5f};
    s.DockingSeparatorSize = 3;

    auto rgb = [](float r, float g, float b, float a = 1.0f) { return ImVec4{r / 255.0f, g / 255.0f, b / 255.0f, a}; };
    const ImVec4 bg = rgb(37, 37, 40), panel = rgb(45, 45, 48), frame = rgb(28, 28, 30), border = rgb(20, 20, 22);
    const ImVec4 raised = rgb(62, 62, 66), hover = rgb(75, 75, 80), accent = rgb(70, 120, 180), accentHi = rgb(90, 145, 210);
    auto* c = s.Colors;
    c[ImGuiCol_Text] = rgb(220, 220, 220);
    c[ImGuiCol_TextDisabled] = rgb(128, 128, 132);
    c[ImGuiCol_WindowBg] = panel;
    c[ImGuiCol_ChildBg] = rgb(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = rgb(40, 40, 43, 0.98f);
    c[ImGuiCol_Border] = border;
    c[ImGuiCol_BorderShadow] = rgb(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = rgb(40, 40, 44);
    c[ImGuiCol_FrameBgActive] = rgb(48, 48, 54);
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = rgb(45, 55, 70);
    c[ImGuiCol_TitleBgCollapsed] = bg;
    c[ImGuiCol_MenuBarBg] = rgb(32, 32, 35);
    c[ImGuiCol_ScrollbarBg] = frame;
    c[ImGuiCol_ScrollbarGrab] = raised;
    c[ImGuiCol_ScrollbarGrabHovered] = hover;
    c[ImGuiCol_ScrollbarGrabActive] = accent;
    c[ImGuiCol_CheckMark] = accentHi;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentHi;
    c[ImGuiCol_Button] = raised;
    c[ImGuiCol_ButtonHovered] = hover;
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Header] = rgb(55, 65, 80);
    c[ImGuiCol_HeaderHovered] = rgb(65, 80, 100);
    c[ImGuiCol_HeaderActive] = accent;
    c[ImGuiCol_Separator] = border;
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accentHi;
    c[ImGuiCol_ResizeGrip] = rgb(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = accent;
    c[ImGuiCol_ResizeGripActive] = accentHi;
    c[ImGuiCol_Tab] = bg;
    c[ImGuiCol_TabHovered] = hover;
    c[ImGuiCol_TabSelected] = panel;
    c[ImGuiCol_TabSelectedOverline] = accentHi;
    c[ImGuiCol_TabDimmed] = bg;
    c[ImGuiCol_TabDimmedSelected] = panel;
    c[ImGuiCol_TabDimmedSelectedOverline] = rgb(0, 0, 0, 0);
    c[ImGuiCol_DockingPreview] = rgb(70, 120, 180, 0.6f);
    c[ImGuiCol_DockingEmptyBg] = rgb(20, 20, 22);
    c[ImGuiCol_TableHeaderBg] = bg;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_TableBorderLight] = rgb(35, 35, 38);
    c[ImGuiCol_TextSelectedBg] = rgb(70, 120, 180, 0.45f);
    c[ImGuiCol_NavCursor] = accentHi;
}

} // namespace de::app
