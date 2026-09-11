// Easel — ui.h  一层薄词汇（D-08：是词汇不是墙）
//
// 每个函数都只有十几行，包的就是 ImGui。Easel 没有的东西，直接写 ImGui::XXX，
// 两者可以混在一起。ImGui 的示例窗口（F12 -> 帮助）就是活的参考手册。
#ifndef EASEL_UI_H
#define EASEL_UI_H

#include <easel/core.h>

#include <initializer_list>

namespace easel {
namespace ui {

// 折叠分组。返回 true 表示展开着，里面的控件才需要画。
//     if (ui::section("参数")) { ... }
bool section(const char* label, bool defaultOpen = true);

// 滑块。变量是唯一真相源：拖滑块就改变量，改变量滑块也跟着动。
bool slider(const char* label, double* v, double lo, double hi, const char* fmt = "%.2f");
bool slider(const char* label, float* v, float lo, float hi, const char* fmt = "%.2f");
bool slider(const char* label, int* v, int lo, int hi);

// 和 slider 一模一样，但只在**松手那一帧**返回 true（其余每一帧、包括拖动中，都返回
// false）。参数改动会触发重算（重新跑一遍模拟/优化）时用它——slider 每帧都 true，
// 拖一下中间的几十帧全部重算一遍，重的作品会卡成幻灯片；sliderCommit 只在松手那一刻
// 触发一次。想要「拖的时候就能实时看到」还是用 slider。
bool sliderCommit(const char* label, double* v, double lo, double hi, const char* fmt = "%.2f");
bool sliderCommit(const char* label, float* v, float lo, float hi, const char* fmt = "%.2f");
bool sliderCommit(const char* label, int* v, int lo, int hi);

bool toggle(const char* label, bool* v);
bool button(const char* label, bool wide = false);

// 下拉框（N 选一）。和 slider 一样，*v 是唯一真相源（存的是选中项的下标），
// 返回 true 表示这一帧选项变了：
//     if (ui::select("算法", &S.algo, {"冒泡排序", "选择排序"})) sortAll();
// 选项数量固定、写在代码里就用 initializer_list 这版，最短；选项是运行时算出来的
// （文件名列表、Theme::presets() 之类）就用 vector<string> 这版。
bool select(const char* label, int* v, std::initializer_list<const char*> items);
bool select(const char* label, int* v, const std::vector<std::string>& items);

// 单行文字输入。v 是唯一真相源，内部自己管缓冲区——不用像 ImGui::InputText 那样
// 自己开 char buf[N]、传 sizeof buf，长度也不再被 N 卡死。
// input 只要这一帧内容变了（敲一个字符、删一个字符……）就返回 true，跟 slider
// 拖动中每帧都 true 是一个道理，适合「打字的时候画面就跟着变」（01-dancing-name
// 里要显示的文字）。要是这行字只是用来触发一次性的动作（重新生成、重新计算），
// 每敲一个字符都触发一遍既没意义又浪费，这时候用 inputCommit——和 sliderCommit
// 对 slider 的关系一样，只在敲完回车或失焦离开那一刻返回 true，其余帧都是 false
//（09-monster-maker 里输入种子复现）。
bool input(const char* label, std::string* v);
bool inputCommit(const char* label, std::string* v);

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
