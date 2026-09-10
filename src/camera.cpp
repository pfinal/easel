// Easel — camera.cpp
#include <easel/camera.h>

#include <imgui.h>

namespace easel {

void Camera::fit(const Rect& worldRect, double paddingPx) {
    // 视口还没算出来（run() 之前、或者窗口刚建好那一瞬间）：先记下来，第一帧再执行。
    // 没有这一步的话，「main 里 fit 一下再 run()」会得到一个荒唐的缩放值。
    if (viewport_.w <= 2 || viewport_.h <= 2) {
        pendingFit_ = true;
        pendingRect_ = worldRect;
        pendingPad_ = paddingPx;
        target_ = worldRect.center();
        return;
    }
    if (worldRect.w <= 0 || worldRect.h <= 0) {
        center(worldRect.center());
        return;
    }
    double aw = std::max(1.0, viewport_.w - 2 * paddingPx);
    double ah = std::max(1.0, viewport_.h - 2 * paddingPx);
    scale_ = std::min(aw / worldRect.w, ah / worldRect.h);
    scale_ = clamp(scale_, 1e-6, 1e6);
    target_ = worldRect.center();
}

void Camera::setViewport(const Rect& px) {
    viewport_ = px;
    if (pendingFit_ && px.w > 2 && px.h > 2) {
        pendingFit_ = false;
        fit(pendingRect_, pendingPad_);
    }
}

void Camera::center(const Vec2& w) { target_ = w; pendingFit_ = false; }

void Camera::zoom(double pixelsPerUnit) { scale_ = clamp(pixelsPerUnit, 1e-6, 1e6); pendingFit_ = false; }

void Camera::scaleBar(double* metersPerUnit, const char* unitName) {
    metersPerUnit_ = metersPerUnit;
    unitName_ = unitName ? unitName : "米";
}

Vec2 Camera::toScreen(const Vec2& w) const {
    Vec2 c = viewport_.center();
    return {c.x + (w.x - target_.x) * scale_, c.y + (w.y - target_.y) * scale_};
}

Vec2 Camera::toWorld(const Vec2& s) const {
    Vec2 c = viewport_.center();
    return {target_.x + (s.x - c.x) / scale_, target_.y + (s.y - c.y) / scale_};
}

Rect Camera::visibleWorld() const {
    Vec2 a = toWorld({viewport_.x, viewport_.y});
    Vec2 b = toWorld({viewport_.right(), viewport_.bottom()});
    return Rect::fromCorners(a, b);
}

// 滚轮缩放（以鼠标为中心）、中键拖拽平移、空格 + 左键平移。
void Camera::handleInput(bool hovered) {
    if (!panZoom_) return;
    ImGuiIO& io = ImGui::GetIO();

    if (hovered && io.MouseWheel != 0.0f) {
        Vec2   m{io.MousePos.x, io.MousePos.y};
        Vec2   before = toWorld(m);
        double factor = std::pow(1.15, (double)io.MouseWheel);
        zoom(scale_ * factor);
        Vec2 after = toWorld(m);
        target_ += before - after;   // 让鼠标下的那个点原地不动
    }

    bool spacePan = ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if ((hovered || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || spacePan)) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        target_ -= Vec2{d.x / scale_, d.y / scale_};
    }
}

}  // namespace easel
