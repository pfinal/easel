// Easel — theme.h  主题即数据（D-22：同届学生各选一个，界面不撞脸）
#ifndef EASEL_THEME_H
#define EASEL_THEME_H

#include <easel/core.h>

namespace easel {

struct Theme {
    // ---- 品牌 ----
    Color       accent  = Color::hex(0x2E7D32);   // 主色：按钮、高亮、选中
    Color       accent2 = Color::hex(0xF57C00);   // 次色：对比、警示曲线
    bool        dark    = true;
    float       radius  = 6.f;                    // 圆角
    float       fontSize = 17.f;                  // 逻辑像素，HiDPI 会自动乘系数
    std::string fontPath;                         // 留空 = 自动找系统中文字体

    // ---- 语义色板 ----
    Color bg      = Color::hex(0x14171A);   // 画布背景
    Color surface = Color::hex(0x1E2227);   // 面板 / 卡片
    Color fg      = Color::hex(0xE8EAED);   // 正文
    Color muted   = Color::hex(0x8B939C);   // 次要文字、网格
    Color good    = Color::hex(0x43A047);
    Color warn    = Color::hex(0xFB8C00);
    Color bad     = Color::hex(0xE53935);

    std::string name = "Forest";

    static Theme Forest();   // 绿 · 深色（默认）
    static Theme Ocean();    // 蓝 · 深色
    static Theme Ember();    // 橙 · 深色
    static Theme Paper();    // 米白 · 浅色
    static Theme Slate();    // 灰蓝 · 浅色
    static std::vector<Theme> presets();
};

}  // namespace easel
#endif
