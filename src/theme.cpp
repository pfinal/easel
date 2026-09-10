// Easel — theme.cpp  五个预设 + 把主题刷到 ImGui 上
#include "internal.h"

#include <easel/theme.h>

namespace easel {

Theme Theme::Forest() {
    Theme t;
    t.name = "Forest";
    t.dark = true;
    t.accent = Color::hex(0x43A047);
    t.accent2 = Color::hex(0xF9A825);
    t.bg = Color::hex(0x11150F);
    t.surface = Color::hex(0x1B2119);
    t.fg = Color::hex(0xE6EDE3);
    t.muted = Color::hex(0x86937F);
    t.good = Color::hex(0x66BB6A);
    t.warn = Color::hex(0xFFB300);
    t.bad = Color::hex(0xE53935);
    return t;
}

Theme Theme::Ocean() {
    Theme t;
    t.name = "Ocean";
    t.dark = true;
    t.accent = Color::hex(0x29B6F6);
    t.accent2 = Color::hex(0xFF7043);
    t.bg = Color::hex(0x0D1620);
    t.surface = Color::hex(0x16222E);
    t.fg = Color::hex(0xE1ECF4);
    t.muted = Color::hex(0x7C93A6);
    t.good = Color::hex(0x26C6DA);
    t.warn = Color::hex(0xFFA726);
    t.bad = Color::hex(0xEF5350);
    return t;
}

Theme Theme::Ember() {
    Theme t;
    t.name = "Ember";
    t.dark = true;
    t.accent = Color::hex(0xFF7043);
    t.accent2 = Color::hex(0x42A5F5);
    t.bg = Color::hex(0x1A1210);
    t.surface = Color::hex(0x261B18);
    t.fg = Color::hex(0xF3E7E2);
    t.muted = Color::hex(0xA1897F);
    t.good = Color::hex(0x9CCC65);
    t.warn = Color::hex(0xFFCA28);
    t.bad = Color::hex(0xE53935);
    return t;
}

Theme Theme::Paper() {
    Theme t;
    t.name = "Paper";
    t.dark = false;
    t.accent = Color::hex(0x2E7D32);
    t.accent2 = Color::hex(0xC62828);
    t.bg = Color::hex(0xF7F5EF);
    t.surface = Color::hex(0xFFFFFF);
    t.fg = Color::hex(0x22262B);
    t.muted = Color::hex(0x6E7781);
    t.good = Color::hex(0x2E7D32);
    t.warn = Color::hex(0xB26A00);
    t.bad = Color::hex(0xC62828);
    t.radius = 5.f;
    return t;
}

Theme Theme::Slate() {
    Theme t;
    t.name = "Slate";
    t.dark = false;
    t.accent = Color::hex(0x3F6DB5);
    t.accent2 = Color::hex(0xD9822B);
    t.bg = Color::hex(0xEDF1F5);
    t.surface = Color::hex(0xFDFEFF);
    t.fg = Color::hex(0x1F2933);
    t.muted = Color::hex(0x69737D);
    t.good = Color::hex(0x2F855A);
    t.warn = Color::hex(0xC05621);
    t.bad = Color::hex(0xC53030);
    t.radius = 4.f;
    return t;
}

std::vector<Theme> Theme::presets() { return {Forest(), Ocean(), Ember(), Paper(), Slate()}; }

namespace internal {

void applyTheme(const Theme& t) {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark();

    s.WindowRounding = t.radius;
    s.ChildRounding = t.radius;
    s.FrameRounding = t.radius;
    s.PopupRounding = t.radius;
    s.GrabRounding = t.radius;
    s.TabRounding = t.radius;
    s.ScrollbarRounding = t.radius;
    s.WindowBorderSize = 0.f;
    s.FrameBorderSize = 0.f;
    s.WindowPadding = ImVec2(14, 12);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(9, 8);
    s.ItemInnerSpacing = ImVec2(7, 5);
    s.ScrollbarSize = 12.f;
    s.GrabMinSize = 10.f;
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);

    const Color surf = t.surface;
    const Color bg = t.bg;
    const Color fg = t.fg;
    const Color mut = t.muted;
    const Color acc = t.accent;
    const Color field = t.dark ? surf.lighter(0.08f) : bg.darker(0.03f);
    const Color hover = t.dark ? surf.lighter(0.16f) : bg.darker(0.08f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = iv4(fg);
    c[ImGuiCol_TextDisabled] = iv4(mut);
    c[ImGuiCol_WindowBg] = iv4(surf);
    c[ImGuiCol_ChildBg] = iv4(surf);
    c[ImGuiCol_PopupBg] = iv4(t.dark ? surf.lighter(0.05f) : surf);
    c[ImGuiCol_Border] = iv4(mut.withAlpha(t.dark ? 0.22f : 0.30f));
    c[ImGuiCol_FrameBg] = iv4(field);
    c[ImGuiCol_FrameBgHovered] = iv4(hover);
    c[ImGuiCol_FrameBgActive] = iv4(acc.withAlpha(0.35f));
    c[ImGuiCol_TitleBg] = iv4(surf);
    c[ImGuiCol_TitleBgActive] = iv4(surf);
    c[ImGuiCol_TitleBgCollapsed] = iv4(surf);
    c[ImGuiCol_MenuBarBg] = iv4(surf);
    c[ImGuiCol_ScrollbarBg] = iv4(surf);
    c[ImGuiCol_ScrollbarGrab] = iv4(mut.withAlpha(0.35f));
    c[ImGuiCol_ScrollbarGrabHovered] = iv4(mut.withAlpha(0.55f));
    c[ImGuiCol_ScrollbarGrabActive] = iv4(acc);
    c[ImGuiCol_CheckMark] = iv4(acc);
    c[ImGuiCol_SliderGrab] = iv4(acc);
    c[ImGuiCol_SliderGrabActive] = iv4(acc.lighter(0.2f));
    c[ImGuiCol_Button] = iv4(field);
    c[ImGuiCol_ButtonHovered] = iv4(acc.withAlpha(0.55f));
    c[ImGuiCol_ButtonActive] = iv4(acc);
    c[ImGuiCol_Header] = iv4(t.dark ? surf.lighter(0.10f) : bg.darker(0.05f));
    c[ImGuiCol_HeaderHovered] = iv4(acc.withAlpha(0.35f));
    c[ImGuiCol_HeaderActive] = iv4(acc.withAlpha(0.55f));
    c[ImGuiCol_Separator] = iv4(mut.withAlpha(0.25f));
    c[ImGuiCol_SeparatorHovered] = iv4(acc.withAlpha(0.6f));
    c[ImGuiCol_SeparatorActive] = iv4(acc);
    c[ImGuiCol_ResizeGrip] = iv4(mut.withAlpha(0.20f));
    c[ImGuiCol_ResizeGripHovered] = iv4(acc.withAlpha(0.6f));
    c[ImGuiCol_ResizeGripActive] = iv4(acc);
    c[ImGuiCol_Tab] = iv4(t.dark ? surf.darker(0.15f) : bg.darker(0.04f));
    c[ImGuiCol_TabHovered] = iv4(acc.withAlpha(0.5f));
    c[ImGuiCol_TabSelected] = iv4(surf);
    c[ImGuiCol_TabDimmed] = iv4(t.dark ? surf.darker(0.2f) : bg);
    c[ImGuiCol_TabDimmedSelected] = iv4(surf);
    c[ImGuiCol_PlotLines] = iv4(acc);
    c[ImGuiCol_PlotLinesHovered] = iv4(t.accent2);
    c[ImGuiCol_PlotHistogram] = iv4(acc);
    c[ImGuiCol_PlotHistogramHovered] = iv4(t.accent2);
    c[ImGuiCol_TableHeaderBg] = iv4(t.dark ? surf.lighter(0.08f) : bg.darker(0.03f));
    c[ImGuiCol_TableBorderStrong] = iv4(mut.withAlpha(0.3f));
    c[ImGuiCol_TableBorderLight] = iv4(mut.withAlpha(0.15f));
    c[ImGuiCol_TextSelectedBg] = iv4(acc.withAlpha(0.35f));
    c[ImGuiCol_NavCursor] = iv4(acc);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, t.dark ? 0.55f : 0.35f);

    // ImPlot 跟着主题走
    ImPlotStyle& ps = ImPlot::GetStyle();
    ps.PlotPadding = ImVec2(8, 8);
    ImVec4* pc = ps.Colors;
    pc[ImPlotCol_FrameBg] = iv4(t.dark ? surf.darker(0.25f) : bg.darker(0.02f));
    pc[ImPlotCol_PlotBg] = iv4(t.dark ? surf.darker(0.3f) : bg.darker(0.01f));
    pc[ImPlotCol_PlotBorder] = iv4(mut.withAlpha(0.25f));
    pc[ImPlotCol_AxisText] = iv4(mut);
    pc[ImPlotCol_AxisGrid] = iv4(mut.withAlpha(0.18f));
    pc[ImPlotCol_LegendBg] = iv4(surf.withAlpha(0.9f));
    pc[ImPlotCol_LegendText] = iv4(fg);
}

}  // namespace internal
}  // namespace easel
