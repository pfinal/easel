// ============================================================================
//  Easel — 「画架」
//  一个为算法可视化程序做的调试器，顺便带一层薄薄的界面。
//  代码酷 daimaku.net · MIT · https://github.com/pfinal/easel
//
//      #include <easel/easel.h>
//
//  第一次用请看 docs/cheatsheet.md（一页 API + 一页调试指南）。
// ============================================================================
#ifndef EASEL_H
#define EASEL_H

#include <easel/core.h>      // 无 GUI：Vec2 / Rect / Color / json / LOG / TRACE / CHECK / 对拍
#include <easel/theme.h>
#include <easel/camera.h>
#include <easel/canvas.h>
#include <easel/timeline.h>
#include <easel/ui.h>
#include <easel/file.h>
#include <easel/audio.h>
#include <easel/bench.h>
#include <easel/app.h>

// 铁律（D-08）：ImGui 对你永远是开着的。
// Easel 没有的控件，直接写 ImGui::XXX / ImPlot::XXX，可以和 easel::ui 混在一起。
// F12 -> 帮助 里能打开 ImGui 和 ImPlot 的示例窗口，那是活的参考手册。
#include <imgui.h>
#include <implot.h>

#endif
