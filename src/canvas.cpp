// Easel — canvas.cpp
#include "internal.h"

namespace easel {

using internal::col;
using internal::ev;
using internal::iv;

namespace {
constexpr double kTau = 6.283185307179586;   // 一整圈的弧度

// 椭圆上采一圈世界点。圆就是 rx == ry 的椭圆。
void ellipseWorldPoints(const Vec2& c, double rx, double ry, int seg, std::vector<Vec2>& out) {
    out.clear();
    out.reserve((size_t)seg);
    for (int i = 0; i < seg; ++i) {
        double t = kTau * i / seg;
        out.push_back({c.x + rx * std::cos(t), c.y + ry * std::sin(t)});
    }
}
}  // namespace

void Canvas::begin(Camera& cam, void* drawList, bool hovered, float dpiScale) {
    cam_ = &cam;
    dl_ = drawList;
    hovered_ = hovered;
    dpi_ = dpiScale > 0.f ? dpiScale : 1.f;
    st_ = Style{};
    mat_ = Mat{};
    stack_.clear();
    shape_.clear();
    inShape_ = false;
    stats_ = Stats{};
}

void Canvas::end() { dl_ = nullptr; }

// ---------------------------------------------------------------- 坐标
Vec2    Canvas::toScreen(const Vec2& w) const { return cam_ ? cam_->toScreen(w) : w; }
Vec2    Canvas::toWorld(const Vec2& s) const { return cam_ ? cam_->toWorld(s) : s; }
Rect    Canvas::world() const { return cam_ ? cam_->visibleWorld() : Rect{}; }
Rect    Canvas::screen() const { return cam_ ? cam_->viewport() : Rect{}; }
double  Canvas::zoom() const { return cam_ ? cam_->zoom() : 1.0; }
bool    Canvas::hovered() const { return hovered_; }
Camera& Canvas::camera() const { return *cam_; }

Vec2 Canvas::mouse() const {
    ImVec2 m = ImGui::GetIO().MousePos;
    return toWorld({m.x, m.y});
}

// ---------------------------------------------------------------- 样式
Canvas& Canvas::fill(const Color& c) { st_.fillColor = c; st_.hasFill = true; return *this; }
Canvas& Canvas::noFill() { st_.hasFill = false; return *this; }
Canvas& Canvas::stroke(const Color& c, double widthPx) {
    st_.strokeColor = c; st_.strokeWidth = widthPx; st_.hasStroke = true; return *this;
}
Canvas& Canvas::stroke(const Color& c) { st_.strokeColor = c; st_.hasStroke = true; return *this; }
Canvas& Canvas::noStroke() { st_.hasStroke = false; return *this; }
Canvas& Canvas::strokeWidth(double px) { st_.strokeWidth = px; return *this; }
Canvas& Canvas::alpha(double a) { st_.alpha = clamp(a, 0.0, 1.0); return *this; }
Canvas& Canvas::textSize(double px) { st_.fontPx = px; return *this; }
Canvas& Canvas::dashed(double onPx, double offPx) { st_.dashOn = onPx; st_.dashOff = offPx; return *this; }
Canvas& Canvas::solid() { st_.dashOn = st_.dashOff = 0; return *this; }
// push/pop 存的是「现场」= 样式 + 矩阵。分开存会让 translate 之后忘记还原，
// 那是 Processing 里最常见的一类 bug，一起存就不会有。
Canvas& Canvas::push() { stack_.push_back(Saved{st_, mat_}); return *this; }
Canvas& Canvas::pop() {
    if (!stack_.empty()) { st_ = stack_.back().st; mat_ = stack_.back().mat; stack_.pop_back(); }
    return *this;
}

Color Canvas::applyAlpha(const Color& c) const { return c.withAlpha((float)(c.a * st_.alpha)); }

// ---------------------------------------------------------------- 变换栈
// M = M × T：新的变换乘在右边，所以「后写的先作用在点上」。
// c.translate(p).rotate(a) 读起来是「挪到 p，再转 a」，实际是「先把点转 a，再挪到 p」——
// 两种说法画出来是同一个东西，这正是 Processing 的语义。
Canvas& Canvas::translate(const Vec2& d) {
    mat_ = Mat::mul(mat_, Mat{1, 0, 0, 1, d.x, d.y});
    return *this;
}
Canvas& Canvas::rotate(double radians) {
    double c = std::cos(radians), s = std::sin(radians);
    mat_ = Mat::mul(mat_, Mat{c, s, -s, c, 0, 0});
    return *this;
}
Canvas& Canvas::scale(double s) { return scale(s, s); }
Canvas& Canvas::scale(double sx, double sy) {
    mat_ = Mat::mul(mat_, Mat{sx, 0, 0, sy, 0, 0});
    return *this;
}
Canvas& Canvas::resetMatrix() { mat_ = Mat{}; return *this; }
Vec2 Canvas::transform(const Vec2& p) const { return mat_.apply(p); }

// 世界点 -> 过矩阵 -> 屏幕像素。所有图元都走这一条路。
Vec2 Canvas::project(const Vec2& w) const { return toScreen(mat_.apply(w)); }

// ---------------------------------------------------------------- 内部：一条（可能是虚线的）线段
void Canvas::strokeSegment(const Vec2& sa, const Vec2& sb) {
    ImDrawList* dl = (ImDrawList*)dl_;
    ImU32       c = col(applyAlpha(st_.strokeColor));
    float       w = (float)(st_.strokeWidth * dpi_);
    if (st_.dashOn <= 0) {
        dl->AddLine(iv(sa), iv(sb), c, w);
        return;
    }
    double on = st_.dashOn * dpi_, off = st_.dashOff * dpi_;
    double total = dist(sa, sb);
    if (total < 1e-6) return;
    Vec2   dir = (sb - sa) / total;
    double t = 0;
    int    guard = 0;
    while (t < total && guard++ < 4000) {
        double e = std::min(t + on, total);
        dl->AddLine(iv(sa + dir * t), iv(sa + dir * e), c, w);
        t = e + off;
    }
}

// ---------------------------------------------------------------- 内部：一圈屏幕点的填充 / 描边
// 用 ImGui 的 _Path 当缓冲区，省掉每次调用的临时分配（Layer 重放几万条时这点很值）。
void Canvas::fillScreenPoly(const std::vector<Vec2>& sp, bool convex) {
    if (sp.size() < 3) return;
    ImDrawList* dl = (ImDrawList*)dl_;
    dl->PathClear();
    for (const Vec2& p : sp) dl->PathLineTo(iv(p));
    ImU32 c = col(applyAlpha(st_.fillColor));
    if (convex) dl->PathFillConvex(c);
    else        dl->PathFillConcave(c);   // 凹多边形也画得对，代价是慢一点
}

void Canvas::strokeScreenPoly(const std::vector<Vec2>& sp, bool closed) {
    if (sp.size() < 2 || !st_.hasStroke || st_.strokeWidth <= 0) return;
    ImDrawList* dl = (ImDrawList*)dl_;
    if (st_.dashOn > 0) {
        for (size_t i = 0; i + 1 < sp.size(); ++i) strokeSegment(sp[i], sp[i + 1]);
        if (closed) strokeSegment(sp.back(), sp.front());
        return;
    }
    dl->PathClear();
    for (const Vec2& p : sp) dl->PathLineTo(iv(p));
    dl->PathStroke(col(applyAlpha(st_.strokeColor)), closed ? ImDrawFlags_Closed : 0,
                   (float)(st_.strokeWidth * dpi_));
}

// ---------------------------------------------------------------- 图元
void Canvas::line(const Vec2& a, const Vec2& b) {
    if (!dl_ || !st_.hasStroke) return;
    Vec2 sa = project(a), sb = project(b);
    ++stats_.primitives;
    ++stats_.lines;
    Rect vp = screen();
    if (!vp.contains(sa) && !vp.contains(sb) && !Rect::fromCorners(sa, sb).overlaps(vp)) ++stats_.offscreen;
    if (st_.strokeWidth <= 0 || st_.alpha <= 0) { ++stats_.invisible; return; }
    strokeSegment(sa, sb);
}

void Canvas::polyline(const std::vector<Vec2>& pts, bool closed) {
    if (!dl_ || pts.size() < 2) return;
    if (!st_.hasStroke) return;
    ImDrawList* dl = (ImDrawList*)dl_;
    ++stats_.primitives;
    stats_.lines += (int)pts.size();
    if (st_.strokeWidth <= 0 || st_.alpha <= 0) { ++stats_.invisible; return; }

    if (st_.dashOn > 0) {
        for (size_t i = 0; i + 1 < pts.size(); ++i) strokeSegment(project(pts[i]), project(pts[i + 1]));
        if (closed) strokeSegment(project(pts.back()), project(pts.front()));
        return;
    }
    // 实线走 ImGui 的折线，接头更好看
    dl->PathClear();
    for (const Vec2& p : pts) dl->PathLineTo(iv(project(p)));
    dl->PathStroke(col(applyAlpha(st_.strokeColor)), closed ? ImDrawFlags_Closed : 0,
                   (float)(st_.strokeWidth * dpi_));
}

void Canvas::circle(const Vec2& c, double radiusWorld) {
    if (!dl_) return;
    ImDrawList* dl = (ImDrawList*)dl_;
    ++stats_.primitives;
    ++stats_.circles;
    // 有旋转 / 非等比缩放时，圆在屏幕上不再是圆 —— 退化成多边形来画。
    if (!mat_.identity()) {
        double            sc = std::sqrt(std::fabs(mat_.a * mat_.d - mat_.b * mat_.c));
        double            rs = radiusWorld * sc * zoom();
        std::vector<Vec2> wp, sp;
        ellipseWorldPoints(c, radiusWorld, radiusWorld, rs > 60 ? 64 : 48, wp);
        sp.reserve(wp.size());
        for (const Vec2& p : wp) sp.push_back(project(p));
        if (!Rect::bounding(sp).overlaps(screen())) ++stats_.offscreen;
        if (rs < 0.05 || st_.alpha <= 0) { ++stats_.invisible; return; }
        if (st_.hasFill) fillScreenPoly(sp, true);
        strokeScreenPoly(sp, true);
        return;
    }
    Vec2  s = toScreen(c);
    float r = (float)(radiusWorld * zoom());
    if (!screen().expanded(r + 2).contains(s)) ++stats_.offscreen;
    if (r < 0.05f || st_.alpha <= 0) { ++stats_.invisible; return; }
    int seg = r > 60 ? 64 : 0;
    if (st_.hasFill) dl->AddCircleFilled(iv(s), r, col(applyAlpha(st_.fillColor)), seg);
    if (st_.hasStroke && st_.strokeWidth > 0)
        dl->AddCircle(iv(s), r, col(applyAlpha(st_.strokeColor)), seg, (float)(st_.strokeWidth * dpi_));
}

// dot 的半径是像素，旋转和缩放都改变不了一个圆点的样子 —— 所以只把位置过一遍矩阵。
void Canvas::dot(const Vec2& c, double radiusPx) {
    if (!dl_) return;
    ImDrawList* dl = (ImDrawList*)dl_;
    Vec2        s = project(c);
    float       r = (float)(radiusPx * dpi_);
    ++stats_.primitives;
    ++stats_.circles;
    if (!screen().expanded(r + 2).contains(s)) ++stats_.offscreen;
    if (r < 0.05f || st_.alpha <= 0) { ++stats_.invisible; return; }
    if (st_.hasFill) dl->AddCircleFilled(iv(s), r, col(applyAlpha(st_.fillColor)), 0);
    if (st_.hasStroke && st_.strokeWidth > 0)
        dl->AddCircle(iv(s), r, col(applyAlpha(st_.strokeColor)), 0, (float)(st_.strokeWidth * dpi_));
}

void Canvas::rect(const Rect& r) {
    if (!dl_) return;
    ImDrawList* dl = (ImDrawList*)dl_;
    ++stats_.primitives;
    ++stats_.rects;
    // 转过之后矩形不再轴对齐，交给四边形去画。
    if (!mat_.identity()) {
        std::vector<Vec2> sp = {project(r.min()), project({r.right(), r.top()}), project(r.max()),
                                project({r.left(), r.bottom()})};
        if (!Rect::bounding(sp).overlaps(screen())) ++stats_.offscreen;
        if (st_.alpha <= 0) { ++stats_.invisible; return; }
        if (st_.hasFill) fillScreenPoly(sp, true);
        strokeScreenPoly(sp, true);
        return;
    }
    Vec2 a = toScreen(r.min()), b = toScreen(r.max());
    if (!Rect::fromCorners(a, b).overlaps(screen())) ++stats_.offscreen;
    if (st_.alpha <= 0) { ++stats_.invisible; return; }
    if (st_.hasFill) dl->AddRectFilled(iv(a), iv(b), col(applyAlpha(st_.fillColor)));
    if (st_.hasStroke && st_.strokeWidth > 0)
        dl->AddRect(iv(a), iv(b), col(applyAlpha(st_.strokeColor)), 0.f, 0, (float)(st_.strokeWidth * dpi_));
}

// 文字永远是正的（旋转的文字 ImGui 画不了），矩阵只决定它落在哪。
void Canvas::text(const Vec2& at, const std::string& s, Align align) {
    if (!dl_ || s.empty()) return;
    ImDrawList* dl = (ImDrawList*)dl_;
    ImFont*     f = ImGui::GetFont();
    float       size = (float)(st_.fontPx * dpi_);
    Vec2        p = project(at);
    ++stats_.primitives;
    ++stats_.texts;
    if (align != Align::Left) {
        ImVec2 sz = f->CalcTextSizeA(size, FLT_MAX, 0.f, s.c_str());
        p.x -= (align == Align::Center) ? sz.x * 0.5 : sz.x;
    }
    if (!screen().expanded(200).contains(p)) ++stats_.offscreen;
    if (st_.alpha <= 0 || size < 1.f) { ++stats_.invisible; return; }
    Color c = st_.hasFill ? st_.fillColor : st_.strokeColor;
    dl->AddText(f, size, iv(p), col(applyAlpha(c)), s.c_str());
}

void Canvas::image(const Texture& t, const Rect& worldRect) {
    image(t, worldRect, Rect(0, 0, (double)t.w, (double)t.h));
}

// 四个角分别过矩阵，所以配合 rotate/scale 就能把图片转起来、翻过来。
void Canvas::image(const Texture& t, const Rect& worldRect, const Rect& srcPx) {
    if (!dl_ || !t || t.w <= 0 || t.h <= 0) return;
    ImDrawList* dl = (ImDrawList*)dl_;
    Vec2        p1 = project(worldRect.min()), p2 = project({worldRect.right(), worldRect.top()});
    Vec2        p3 = project(worldRect.max()), p4 = project({worldRect.left(), worldRect.bottom()});
    ++stats_.primitives;
    ++stats_.images;
    if (!Rect::bounding({p1, p2, p3, p4}).overlaps(screen())) ++stats_.offscreen;
    if (st_.alpha <= 0) { ++stats_.invisible; return; }
    // 源矩形是像素，除以图片尺寸就是 UV（0..1）。
    float u0 = (float)(srcPx.left() / t.w), v0 = (float)(srcPx.top() / t.h);
    float u1 = (float)(srcPx.right() / t.w), v1 = (float)(srcPx.bottom() / t.h);
    dl->AddImageQuad((ImTextureID)t.id, iv(p1), iv(p2), iv(p3), iv(p4), ImVec2(u0, v0), ImVec2(u1, v0),
                     ImVec2(u1, v1), ImVec2(u0, v1), col(Color(1, 1, 1, (float)st_.alpha)));
}

// ---------------------------------------------------------------- 更多形状
void Canvas::triangle(const Vec2& a, const Vec2& b, const Vec2& c) {
    polygon({a, b, c});
}

// Processing 的 polygon：填充 + 描边，凹多边形也认。
void Canvas::polygon(const std::vector<Vec2>& pts) {
    if (!dl_ || pts.size() < 3) return;
    std::vector<Vec2> sp;
    sp.reserve(pts.size());
    for (const Vec2& p : pts) sp.push_back(project(p));
    ++stats_.primitives;
    ++stats_.polygons;
    if (!Rect::bounding(sp).overlaps(screen())) ++stats_.offscreen;
    if (st_.alpha <= 0) { ++stats_.invisible; return; }
    if (st_.hasFill) fillScreenPoly(sp, false);
    strokeScreenPoly(sp, true);
}

// 椭圆：rx / ry 是世界坐标的半轴（不是直径，和 circle 一致）。
void Canvas::ellipse(const Vec2& c, double rxWorld, double ryWorld) {
    if (!dl_ || rxWorld <= 0 || ryWorld <= 0) return;
    double            rs = std::max(rxWorld, ryWorld) * zoom();
    std::vector<Vec2> wp;
    ellipseWorldPoints(c, rxWorld, ryWorld, rs > 60 ? 64 : 48, wp);
    std::vector<Vec2> sp;
    sp.reserve(wp.size());
    for (const Vec2& p : wp) sp.push_back(project(p));
    ++stats_.primitives;
    ++stats_.circles;
    if (!Rect::bounding(sp).overlaps(screen())) ++stats_.offscreen;
    if (rs < 0.05 || st_.alpha <= 0) { ++stats_.invisible; return; }
    if (st_.hasFill) fillScreenPoly(sp, true);
    strokeScreenPoly(sp, true);
}

// 圆弧。a0/a1 是弧度，pie = true 连回圆心画成扇形（填充也按扇形算）。
void Canvas::arc(const Vec2& c, double radiusWorld, double a0, double a1, bool pie) {
    if (!dl_ || radiusWorld <= 0) return;
    ImDrawList* dl = (ImDrawList*)dl_;
    ++stats_.primitives;
    ++stats_.circles;
    double span = std::fabs(a1 - a0);
    int    seg = (int)clamp(std::ceil(span / kTau * 64.0), 4, 64);
    if (st_.alpha <= 0) { ++stats_.invisible; return; }

    std::vector<Vec2> sp;
    if (mat_.identity()) {
        // 单位阵时借 ImGui 的 PathArcTo 采样（它的分段和抗锯齿最准），再把点取回来。
        dl->PathClear();
        dl->PathArcTo(iv(toScreen(c)), (float)(radiusWorld * zoom()), (float)a0, (float)a1, seg);
        sp.reserve((size_t)dl->_Path.Size);
        for (int i = 0; i < dl->_Path.Size; ++i) sp.push_back(ev(dl->_Path[i]));
        dl->PathClear();
    } else {
        sp.reserve((size_t)seg + 1);
        for (int i = 0; i <= seg; ++i) {
            double t = a0 + (a1 - a0) * i / seg;
            sp.push_back(project({c.x + radiusWorld * std::cos(t), c.y + radiusWorld * std::sin(t)}));
        }
    }
    if (pie) sp.insert(sp.begin(), project(c));
    if (sp.size() < 2) { ++stats_.invisible; return; }
    if (!Rect::bounding(sp).overlaps(screen())) ++stats_.offscreen;
    if (st_.hasFill && sp.size() >= 3) fillScreenPoly(sp, false);
    strokeScreenPoly(sp, pie);
}

// 三次贝塞尔曲线（只描边，和 Processing 一样）。
// 仿射变换和贝塞尔可以交换顺序，所以把四个控制点变换过去，曲线就是对的。
void Canvas::bezier(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3) {
    if (!dl_ || !st_.hasStroke) return;
    ImDrawList* dl = (ImDrawList*)dl_;
    ++stats_.primitives;
    ++stats_.lines;
    if (st_.alpha <= 0 || st_.strokeWidth <= 0) { ++stats_.invisible; return; }
    Vec2 s0 = project(p0), s1 = project(p1), s2 = project(p2), s3 = project(p3);
    if (!Rect::bounding({s0, s1, s2, s3}).overlaps(screen())) ++stats_.offscreen;
    dl->PathClear();
    dl->PathLineTo(iv(s0));
    dl->PathBezierCubicCurveTo(iv(s1), iv(s2), iv(s3), 0);
    if (st_.dashOn <= 0) {
        dl->PathStroke(col(applyAlpha(st_.strokeColor)), 0, (float)(st_.strokeWidth * dpi_));
        return;
    }
    // 虚线要一段一段量着画，先把 ImGui 采好的点取回来
    std::vector<Vec2> sp;
    sp.reserve((size_t)dl->_Path.Size);
    for (int i = 0; i < dl->_Path.Size; ++i) sp.push_back(ev(dl->_Path[i]));
    dl->PathClear();
    strokeScreenPoly(sp, false);
}

// ---------------------------------------------------------------- 自定义形状
void Canvas::beginShape() {
    shape_.clear();
    inShape_ = true;
}

void Canvas::vertex(const Vec2& p) {
    if (inShape_) shape_.push_back(p);
}

void Canvas::endShape(bool closed) {
    if (!inShape_) return;
    inShape_ = false;
    if (closed) polygon(shape_);      // 闭合就是一个多边形：填充 + 描边
    else        polyline(shape_);     // 不闭合就只是一条折线
    shape_.clear();
}

// ---------------------------------------------------------------- 重放一个 Layer
// 每条命令都 push/pop 一次：Layer 里的样式不该漏到它后面的代码上。
void Canvas::draw(const Layer& layer) {
    if (!dl_) return;
    for (const Layer::Cmd& c : layer.commands()) {
        push();
        setStyle(c.st);
        switch (c.kind) {
            case Layer::Kind::Line:     line(c.p0, c.p1); break;
            case Layer::Kind::Polyline: polyline(c.pts, c.closed); break;
            case Layer::Kind::Circle:   circle(c.p0, c.r1); break;
            case Layer::Kind::Ellipse:  ellipse(c.p0, c.r1, c.r2); break;
            case Layer::Kind::Dot:      dot(c.p0, c.r1); break;
            case Layer::Kind::Rect:     rect(c.box); break;
            case Layer::Kind::Triangle: triangle(c.p0, c.p1, c.p2); break;
            case Layer::Kind::Polygon:  polygon(c.pts); break;
            case Layer::Kind::Text:     text(c.p0, c.txt, c.align); break;
            case Layer::Kind::Image:    image(c.tex, c.box); break;
            case Layer::Kind::ImageSub: image(c.tex, c.box, c.src); break;
        }
        pop();
    }
}

// ================================================================ Layer
// 记一条命令。满了就丢最早的 —— 拖尾自然就有了「只留最近一段」的效果。
Layer::Cmd& Layer::push(Kind k) {
    while (cmds_.size() >= limit_ && !cmds_.empty()) cmds_.pop_front();
    cmds_.emplace_back();
    Cmd& c = cmds_.back();
    c.kind = k;
    c.st = st_;
    return c;
}

void Layer::limit(size_t maxCommands) {
    limit_ = maxCommands < 1 ? 1 : maxCommands;
    while (cmds_.size() > limit_) cmds_.pop_front();
}

void Layer::line(const Vec2& a, const Vec2& b) {
    Cmd& c = push(Kind::Line);
    c.p0 = a;
    c.p1 = b;
}
void Layer::polyline(const std::vector<Vec2>& pts, bool closed) {
    Cmd& c = push(Kind::Polyline);
    c.pts = pts;
    c.closed = closed;
}
void Layer::circle(const Vec2& center, double radiusWorld) {
    Cmd& c = push(Kind::Circle);
    c.p0 = center;
    c.r1 = radiusWorld;
}
void Layer::ellipse(const Vec2& center, double rxWorld, double ryWorld) {
    Cmd& c = push(Kind::Ellipse);
    c.p0 = center;
    c.r1 = rxWorld;
    c.r2 = ryWorld;
}
void Layer::dot(const Vec2& center, double radiusPx) {
    Cmd& c = push(Kind::Dot);
    c.p0 = center;
    c.r1 = radiusPx;
}
void Layer::rect(const Rect& r) {
    Cmd& c = push(Kind::Rect);
    c.box = r;
}
void Layer::triangle(const Vec2& a, const Vec2& b, const Vec2& t) {
    Cmd& c = push(Kind::Triangle);
    c.p0 = a;
    c.p1 = b;
    c.p2 = t;
}
void Layer::polygon(const std::vector<Vec2>& pts) {
    Cmd& c = push(Kind::Polygon);
    c.pts = pts;
}
void Layer::text(const Vec2& at, const std::string& s, Align align) {
    Cmd& c = push(Kind::Text);
    c.p0 = at;
    c.txt = s;
    c.align = align;
}
void Layer::image(const Texture& t, const Rect& worldRect) {
    Cmd& c = push(Kind::Image);
    c.tex = t;
    c.box = worldRect;
}
void Layer::image(const Texture& t, const Rect& worldRect, const Rect& srcPx) {
    Cmd& c = push(Kind::ImageSub);
    c.tex = t;
    c.box = worldRect;
    c.src = srcPx;
}

}  // namespace easel
