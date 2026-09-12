// Easel — camera.h  世界坐标 <-> 屏幕像素
#ifndef EASEL_CAMERA_H
#define EASEL_CAMERA_H

#include <easel/core.h>

namespace easel {

class App;   // 只用来 friend，见下面 setViewport/handleInput

// 相机把「世界坐标」（米、格子、随便什么单位）映射到画布上的像素。
// 世界坐标的 y 轴向下（和屏幕一样），没有翻转。
// 滚轮缩放（以鼠标为中心）和中键 / 空格拖拽平移**默认关着**，要浏览大世界的
// 作品自己 panZoom(true) 打开。
class Camera {
public:
    // ---- 学生会用到的 ----
    // 一键把这块世界区域装进画布：默认严格贴合（paddingPx = 0），worldRect 的四条边正好落在
    // 画布四条边上。如果画面里有贴着边界的「屏幕像素尺寸」图元——dot() 的半径、text() 的字号、
    // 粗线的线宽这些不随缩放变化、世界坐标包围盒又量不到的东西——严格贴合会把它们切掉一截，
    // 这时自己传一个像素留白（比如 fit(rect, 20)）。
    void   fit(const Rect& worldRect, double paddingPx = 0);
    void   center(const Vec2& w);                              // 把这个世界点放到画布中心
    Vec2   center() const { return target_; }
    void   zoom(double pixelsPerUnit);
    double zoom() const { return scale_; }
    // 滚轮缩放 + 中键/空格拖拽平移。**默认关着**：作品多数有固定构图（游戏画面、
    // 图案、网格），滚一下就把画面缩没了。要浏览大世界的作品自己打开它。
    void   panZoom(bool enabled) { panZoom_ = enabled; }
    bool   panZoom() const { return panZoom_; }
    // 显示比例尺。传指针：学生改了变量，比例尺立刻跟着变。
    // metersPerUnit 为 nullptr 表示不显示。
    void scaleBar(double* metersPerUnit, const char* unitName = "米");

    // ---- 坐标换算 ----
    Vec2 toScreen(const Vec2& w) const;
    Vec2 toWorld(const Vec2& s) const;
    Rect visibleWorld() const;                    // 当前看得见的世界范围
    Rect viewport() const { return viewport_; }   // 画布在窗口里的像素矩形

    // ---- Easel 内部调用 ----
    // reset()/scaleBarUnit()/scaleBarName() 也只是给 F12 调试台和状态栏用的内部管线，
    // 但调用点是 app.cpp/debug_console.cpp 里的匿名命名空间自由函数（不是 App 的成员函数），
    // friend class App 罩不到它们，又没必要为了这几行把它们挪成成员函数，所以留在 public
    // （B6：不是每个「内部专用」的东西都能干净地私有化）。
    double*     scaleBarUnit() const { return metersPerUnit_; }
    const char* scaleBarName() const { return unitName_; }
    void        reset() { target_ = {0, 0}; scale_ = 1.0; }

private:
    friend class App;       // setViewport/handleInput 只在 App::run() 里调，其余都不需要
    friend class Graphics;  // Graphics 自己那台 1:1 无缩放的相机，创建/改尺寸时要摆一次 setViewport

    void setViewport(const Rect& px);
    void handleInput(bool hovered);

    Vec2        target_{0, 0};      // 视口中心对应的世界坐标
    double      scale_ = 1.0;       // 一个世界单位 = 多少像素
    Rect        viewport_{0, 0, 1, 1};
    bool        panZoom_ = false;
    double*     metersPerUnit_ = nullptr;
    const char* unitName_ = "米";
    // 窗口还没布局出来的时候（比如 run() 之前就调了 fit），先把请求记下来，
    // 等第一帧有了真正的视口再执行。
    bool   pendingFit_ = false;
    Rect   pendingRect_;
    double pendingPad_ = 0;
};

}  // namespace easel
#endif
