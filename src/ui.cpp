// Easel — ui.cpp
// 每个函数都只有十几行，包的就是 ImGui（D-08：是词汇不是墙）。
// 想要这里没有的控件？直接写 ImGui::XXX，两者可以混着用。
#include "internal.h"

namespace easel {
namespace ui {

bool section(const char* label, bool defaultOpen) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 7));
    bool open = ImGui::CollapsingHeader(label, defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    ImGui::PopStyleVar();
    return open;
}

bool slider(const char* label, double* v, double lo, double hi, const char* fmt) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::TextUnformatted(label);
    std::string id = std::string("##") + label;
    return ImGui::SliderScalar(id.c_str(), ImGuiDataType_Double, v, &lo, &hi, fmt);
}

bool slider(const char* label, float* v, float lo, float hi, const char* fmt) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::TextUnformatted(label);
    std::string id = std::string("##") + label;
    return ImGui::SliderFloat(id.c_str(), v, lo, hi, fmt);
}

bool slider(const char* label, int* v, int lo, int hi) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::TextUnformatted(label);
    std::string id = std::string("##") + label;
    return ImGui::SliderInt(id.c_str(), v, lo, hi);
}

// 三个都是套一层 slider：内部逐帧照常拖动，只是返回值换成
// IsItemDeactivatedAfterEdit()——那个只在「这一帧刚刚松手、且值确实变了」时为 true，
// 拖动过程中的那几十帧全部是 false。参数改动会触发重算的作品用它替掉 slider，
// 免得拖一下中间状态全部重算一遍，卡成幻灯片。
bool sliderCommit(const char* label, double* v, double lo, double hi, const char* fmt) {
    slider(label, v, lo, hi, fmt);
    return ImGui::IsItemDeactivatedAfterEdit();
}

bool sliderCommit(const char* label, float* v, float lo, float hi, const char* fmt) {
    slider(label, v, lo, hi, fmt);
    return ImGui::IsItemDeactivatedAfterEdit();
}

bool sliderCommit(const char* label, int* v, int lo, int hi) {
    slider(label, v, lo, hi);
    return ImGui::IsItemDeactivatedAfterEdit();
}

bool toggle(const char* label, bool* v) { return ImGui::Checkbox(label, v); }

bool button(const char* label, bool wide) {
    return ImGui::Button(label, wide ? ImVec2(-FLT_MIN, 0) : ImVec2(0, 0));
}

// 数据卡：一个大数字 + 单位 + 说明。面板上最像「软件」的那个元素。
void stat(const char* label, const std::string& value, const char* unit) {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::BeginGroup();
    ImGui::TextColored(st.Colors[ImGuiCol_TextDisabled], "%s", label);
    ImGui::PushFont(nullptr, st.FontSizeBase * 1.55f);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopFont();
    if (unit && *unit) {
        ImGui::SameLine(0, 5);
        ImGui::TextColored(st.Colors[ImGuiCol_TextDisabled], "%s", unit);
    }
    ImGui::EndGroup();
}

void stat(const char* label, double value, const char* unit, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, value);
    stat(label, std::string(buf), unit);
}

void stat(const char* label, int value, const char* unit) {
    stat(label, std::to_string(value), unit);
}

namespace {
// 曲线用主题的主色，粗一点，远看也清楚
ImPlotSpec lineSpec() {
    ImPlotSpec spec;
    spec.LineWeight = 2.2f;
    if (App* a = App::instance()) spec.LineColor = internal::iv4(a->theme().accent);
    return spec;
}
}  // namespace

void chart(const char* label, const std::vector<double>& ys, const char* seriesName, float height) {
    if (ys.empty()) { ImGui::TextDisabled("%s：还没有数据", label); return; }
    if (ImPlot::BeginPlot(label, ImVec2(-1, height), ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine(seriesName, ys.data(), (int)ys.size(), 1.0, 0.0, lineSpec());
        ImPlot::EndPlot();
    }
}

void chart(const char* label, const std::vector<double>& xs, const std::vector<double>& ys,
           const char* seriesName, float height) {
    int n = (int)std::min(xs.size(), ys.size());
    if (n == 0) { ImGui::TextDisabled("%s：还没有数据", label); return; }
    if (ImPlot::BeginPlot(label, ImVec2(-1, height), ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine(seriesName, xs.data(), ys.data(), n, lineSpec());
        ImPlot::EndPlot();
    }
}

void separator() { ImGui::Separator(); }
void spacing() { ImGui::Dummy(ImVec2(0, 6)); }
void sameLine() { ImGui::SameLine(); }

void title(const char* text) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void help(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

}  // namespace ui
}  // namespace easel
