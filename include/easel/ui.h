// Easel — ui.h  一层薄词汇（D-08：是词汇不是墙）
//
// 每个函数都只有十几行，包的就是 ImGui。Easel 没有的东西，直接写 ImGui::XXX，
// 两者可以混在一起。ImGui 的示例窗口（F12 -> 帮助）就是活的参考手册。
#ifndef EASEL_UI_H
#define EASEL_UI_H

#include <easel/core.h>

namespace easel {
namespace ui {

// 折叠分组。返回 true 表示展开着，里面的控件才需要画。
//     if (ui::section("参数")) { ... }
bool section(const char* label, bool defaultOpen = true);

// 滑块。变量是唯一真相源：拖滑块就改变量，改变量滑块也跟着动。
bool slider(const char* label, double* v, double lo, double hi, const char* fmt = "%.2f");
bool slider(const char* label, float* v, float lo, float hi, const char* fmt = "%.2f");
bool slider(const char* label, int* v, int lo, int hi);

bool toggle(const char* label, bool* v);
bool button(const char* label, bool wide = false);

// 数据卡：一个大数字 + 单位 + 说明。面板上最像「软件」的那个元素。
void stat(const char* label, const std::string& value, const char* unit = "");
void stat(const char* label, double value, const char* unit = "", int decimals = 2);
void stat(const char* label, int value, const char* unit = "");

// 折线图（ImPlot）。收敛曲线就靠它。
void chart(const char* label, const std::vector<double>& ys, const char* seriesName = "值",
           float height = 140.f);
void chart(const char* label, const std::vector<double>& xs, const std::vector<double>& ys,
           const char* seriesName = "值", float height = 140.f);

// 排版小工具（懒得写 ImGui:: 的时候用）
void separator();
void spacing();
void sameLine();
void title(const char* text);          // 一行小标题
void help(const char* text);           // 灰色说明文字（会自动换行）

}  // namespace ui
}  // namespace easel
#endif
