// Easel — canvas.cpp
#include "internal.h"

// textWorld() 逐字符贴图要拿 ImFontBaked/FindGlyph 量字形，还要 ImTextCharFromUtf8
// 挨个解码码点——这几个都只在 imgui_internal.h 里，公开头文件不碰它，这里是实现细节。
#include <imgui_internal.h>

namespace easel {

using internal::col;
using internal::ev;
using internal::iv;

namespace {
// kTau（一整圈的弧度）现在是 core.h 里的 easel::kTau，这里不再重复定义
// （两份同名常量会让 arc() 里的 `kTau` 变成有歧义的查找，编译不过）。

// 椭圆上采一圈世界点。圆就是 rx == ry 的椭圆。
void ellipseWorldPoints(const Vec2& c, double rx, double ry, int seg, std::vector<Vec2>& out) {
    out.clear();
    out.reserve((size_t)seg);
    for (int i = 0; i < seg; ++i) {
        double t = kTau * i / seg;
        out.push_back({c.x + rx * std::cos(t), c.y + ry * std::sin(t)});
    }
}

// 读一个 UTF-8 码点，返回吃掉的字节数。ASCII 走快路径，其余交给 ImGui 自己的解码器
// （textWorld() 逐字符取字形要一个一个码点地取，标准库没有现成的这个）。
int nextCodepoint(const char* s, const char* end, unsigned int* cp) {
    unsigned char c0 = (unsigned char)*s;
    if (c0 < 0x80) { *cp = c0; return 1; }
    return ImTextCharFromUtf8(cp, s, end);
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

// 当前 textSize() 下量一下这段文字多宽。不画东西，不需要 dl_（画布不在 begin/end
// 之间也能量），只要有字体就行。除以 dpi_ 换回逻辑像素，和 textSize()/text() 的
// 单位系统对上——st_.fontPx * dpi_ 是量的时候用的真实像素，st_.fontPx 本身才是
// 学生设的那个字号。
double Canvas::textWidth(const std::string& s) const {
    if (s.empty()) return 0.0;
    ImFont* f = ImGui::GetFont();
    if (!f) return 0.0;
    float  size = (float)(st_.fontPx * dpi_);
    ImVec2 sz = f->CalcTextSizeA(size, FLT_MAX, 0.f, s.c_str());
    return dpi_ > 0.f ? (double)sz.x / dpi_ : (double)sz.x;
}

// 字号是世界单位，字形跟随当前变换（rotate/scale 都跟）——和 text() 反着来。
// 走的路：逐字符四边形贴图。字体在一个跟当前缩放匹配的像素尺寸下 bake（只影响贴图
// 清不清晰，字最终画多大完全由 sizeWorld 和 mat_/zoom() 决定），每个字符量出它在
// 那个 baked 字号下的 X0/Y0/X1/Y1 偏移和 U/V，换算成世界单位，四个角和 image() 一样
// 都过 mat_ 再 toScreen，用 AddImageQuad 贴上去。比 text() 贵得多（每个字一次 draw
// call），别拿它画成千上万个标签。
void Canvas::textWorld(const Vec2& at, const std::string& s, double sizeWorld, Align align) {
    if (!dl_ || s.empty() || sizeWorld <= 0) return;
    ImDrawList* dl = (ImDrawList*)dl_;
    ImFont*     f = ImGui::GetFont();
    ++stats_.primitives;
    ++stats_.texts;

    // 挑一个够清晰的栅格化尺寸：按当前缩放换算成像素，只决定贴图分辨率。
    float px = (float)(sizeWorld * zoom() * dpi_);
    if (st_.alpha <= 0 || px < 1.f) { ++stats_.invisible; return; }
    ImFontBaked* baked = f->GetFontBaked(px);
    double       k = sizeWorld / (double)baked->Size;   // 「baked 像素」换成「世界单位」

    Color        txt = st_.hasFill ? st_.fillColor : st_.strokeColor;
    ImU32        tint = col(applyAlpha(txt));
    ImTextureRef texRef = f->OwnerAtlas->TexRef;

    std::vector<Vec2> corners;   // 攒起来算这一整段文字的可见性
    size_t             lineStart = 0;
    int                lineIdx = 0;
    while (lineStart <= s.size()) {
        size_t      nl = s.find('\n', lineStart);
        const char* lb = s.c_str() + lineStart;
        const char* le = s.c_str() + (nl == std::string::npos ? s.size() : nl);

        // 第一遍：量这一行的宽度（世界单位），给 Center/Right 对齐用。
        double width = 0;
        for (const char* p = lb; p < le;) {
            unsigned int cp = 0;
            p += nextCodepoint(p, le, &cp);
            width += baked->GetCharAdvance((ImWchar)cp) * k;
        }
        double penX = (align == Align::Center) ? -width * 0.5 : (align == Align::Right) ? -width : 0.0;
        double penY = lineIdx * sizeWorld;   // 行高按字号近似（和 ImGui CalcTextSizeA 一致）

        for (const char* p = lb; p < le;) {
            unsigned int cp = 0;
            p += nextCodepoint(p, le, &cp);
            const ImFontGlyph* g = baked->FindGlyph((ImWchar)cp);
            if (g && g->Visible) {
                double x0 = penX + g->X0 * k, x1 = penX + g->X1 * k;
                double y0 = penY + g->Y0 * k, y1 = penY + g->Y1 * k;
                Vec2 s00 = project(at + Vec2{x0, y0});
                Vec2 s10 = project(at + Vec2{x1, y0});
                Vec2 s11 = project(at + Vec2{x1, y1});
                Vec2 s01 = project(at + Vec2{x0, y1});
                corners.push_back(s00);
                corners.push_back(s11);
                // 彩色字形（罕见，比如表情符号）忽略染色，只保留我们要的 alpha——和
                // ImGui RenderText 对 glyph->Colored 的处理一致。
                ImU32 c2 = g->Colored ? (tint | ~IM_COL32_A_MASK) : tint;
                dl->AddImageQuad(texRef, iv(s00), iv(s10), iv(s11), iv(s01), ImVec2(g->U0, g->V0),
                                 ImVec2(g->U1, g->V0), ImVec2(g->U1, g->V1), ImVec2(g->U0, g->V1), c2);
            }
            penX += baked->GetCharAdvance((ImWchar)cp) * k;
        }

        if (nl == std::string::npos) break;
        lineStart = nl + 1;
        ++lineIdx;
    }
    if (corners.empty()) { ++stats_.invisible; return; }
    if (!Rect::bounding(corners).overlaps(screen())) ++stats_.offscreen;
}

// textWorld() 的配套测量：不画东西，只要知道这段文字在给定世界字号下有多宽。
// 用当前 zoom() 换算出一个够精细的 baked 分辨率（只影响测量精度，不影响结果——
// GetCharAdvance 量出来的值再乘 k 换回世界单位，跟选了哪个 baked 分辨率无关），
// 和 textWorld() 画字用的是同一套字形数据，量出来的宽度就是画出来会占的宽度。
// 多行文字（含 '\n'）取最长一行。
double Canvas::textWidthWorld(const std::string& s, double sizeWorld) const {
    if (s.empty() || sizeWorld <= 0) return 0.0;
    ImFont* f = ImGui::GetFont();
    if (!f) return 0.0;
    float        px = (float)std::max(1.0, sizeWorld * zoom() * (double)dpi_);
    ImFontBaked* baked = f->GetFontBaked(px);
    double       k = sizeWorld / (double)baked->Size;   // 「baked 像素」换成「世界单位」

    double maxWidth = 0.0;
    size_t lineStart = 0;
    while (lineStart <= s.size()) {
        size_t      nl = s.find('\n', lineStart);
        const char* lb = s.c_str() + lineStart;
        const char* le = s.c_str() + (nl == std::string::npos ? s.size() : nl);
        double      width = 0;
        for (const char* p = lb; p < le;) {
            unsigned int cp = 0;
            p += nextCodepoint(p, le, &cp);
            width += baked->GetCharAdvance((ImWchar)cp) * k;
        }
        maxWidth = std::max(maxWidth, width);
        if (nl == std::string::npos) break;
        lineStart = nl + 1;
    }
    return maxWidth;
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
            case Layer::Kind::Arc:      arc(c.p0, c.r1, c.a0, c.a1, c.closed); break;
            case Layer::Kind::Bezier:   bezier(c.p0, c.p1, c.p2, c.p3); break;
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

// 有 xf_（follow() 设的）就把点先过一遍再存；默认 xf_ 是单位阵，行为和以前一样。
void Layer::line(const Vec2& a, const Vec2& b) {
    Cmd& c = push(Kind::Line);
    c.p0 = xf_.apply(a);
    c.p1 = xf_.apply(b);
}
void Layer::polyline(const std::vector<Vec2>& pts, bool closed) {
    Cmd& c = push(Kind::Polyline);
    c.pts.reserve(pts.size());
    for (const Vec2& p : pts) c.pts.push_back(xf_.apply(p));
    c.closed = closed;
}
// 半径按 xf_ 的「统一缩放因子」sqrt(|det|) 一起缩：旋转不改变半径，非等比缩放本来就
// 画不出真圆，这和 Canvas::circle() 遇到非单位阵时的近似处理是同一个道理。
void Layer::circle(const Vec2& center, double radiusWorld) {
    Cmd& c = push(Kind::Circle);
    c.p0 = xf_.apply(center);
    c.r1 = radiusWorld * followScale();
}
void Layer::ellipse(const Vec2& center, double rxWorld, double ryWorld) {
    Cmd& c = push(Kind::Ellipse);
    c.p0 = xf_.apply(center);
    double sc = followScale();
    c.r1 = rxWorld * sc;
    c.r2 = ryWorld * sc;
}
// dot() 的半径是屏幕像素，和 Canvas::dot() 一样不受变换影响，只有中心点跟着走。
void Layer::dot(const Vec2& center, double radiusPx) {
    Cmd& c = push(Kind::Dot);
    c.p0 = xf_.apply(center);
    c.r1 = radiusPx;
}
void Layer::rect(const Rect& r) {
    if (xf_.identity()) {
        Cmd& c = push(Kind::Rect);
        c.box = r;
        return;
    }
    // 转过之后矩形不再轴对齐：存成四个点的多边形（和 Canvas::rect() 遇到旋转时
    // 退化成四边形是同一个道理）。
    Cmd& c = push(Kind::Polygon);
    c.pts = {xf_.apply(r.min()), xf_.apply({r.right(), r.top()}), xf_.apply(r.max()),
              xf_.apply({r.left(), r.bottom()})};
    c.closed = true;
}
void Layer::triangle(const Vec2& a, const Vec2& b, const Vec2& t) {
    Cmd& c = push(Kind::Triangle);
    c.p0 = xf_.apply(a);
    c.p1 = xf_.apply(b);
    c.p2 = xf_.apply(t);
}
void Layer::polygon(const std::vector<Vec2>& pts) {
    Cmd& c = push(Kind::Polygon);
    c.pts.reserve(pts.size());
    for (const Vec2& p : pts) c.pts.push_back(xf_.apply(p));
}
// 和 Canvas::text() 一样，只有锚点跟变换——字形本身不转。follow() 挪得动这行字，
// 转不动这行字的字形；想要字形也转用 c.textWorld()（Layer 存不了那个）。
void Layer::text(const Vec2& at, const std::string& s, Align align) {
    Cmd& c = push(Kind::Text);
    c.p0 = xf_.apply(at);
    c.txt = s;
    c.align = align;
}
Rect Layer::followedBox(const Rect& r) const {
    if (xf_.identity()) return r;
    Vec2 a = xf_.apply(r.min()), b = xf_.apply({r.right(), r.top()});
    Vec2 cc = xf_.apply(r.max()), d = xf_.apply({r.left(), r.bottom()});
    return Rect::bounding({a, b, cc, d});   // 有旋转时只是包围盒，见头文件里的说明
}
void Layer::image(const Texture& t, const Rect& worldRect) {
    Cmd& c = push(Kind::Image);
    c.tex = t;
    c.box = followedBox(worldRect);
}
void Layer::image(const Texture& t, const Rect& worldRect, const Rect& srcPx) {
    Cmd& c = push(Kind::ImageSub);
    c.tex = t;
    c.box = followedBox(worldRect);
    c.src = srcPx;
}
// 圆心和半径的处理同 circle()；角度还要加上 xf_ 的旋转角，不然「转起来」只挪了
// 圆心、弧本身的朝向没跟着转。
void Layer::arc(const Vec2& c, double radiusWorld, double a0, double a1, bool pie) {
    Cmd& cmd = push(Kind::Arc);
    double rot = followRotation();
    cmd.p0 = xf_.apply(c);
    cmd.r1 = radiusWorld * followScale();
    cmd.a0 = a0 + rot;
    cmd.a1 = a1 + rot;
    cmd.closed = pie;
}
// 贝塞尔是精确的：仿射变换和三次贝塞尔可以交换顺序（和 Canvas::bezier() 的注释
// 是同一个道理），四个控制点各自过 xf_ 就行，不用像 circle/arc 那样近似。
void Layer::bezier(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3) {
    Cmd& c = push(Kind::Bezier);
    c.p0 = xf_.apply(p0);
    c.p1 = xf_.apply(p1);
    c.p2 = xf_.apply(p2);
    c.p3 = xf_.apply(p3);
}
// 和 Canvas 的 beginShape/vertex/endShape 一样的状态机，只是攒点的地方换成了
// Layer 自己的 shape_（vertex() 落进去的时候就过一遍 xf_，endShape() 直接存，不用
// 再借 Canvas 的 polygon()/polyline() 转一遍，免得二次变换）。
void Layer::beginShape() {
    shape_.clear();
    inShape_ = true;
}
void Layer::vertex(const Vec2& p) {
    if (inShape_) shape_.push_back(xf_.apply(p));
}
void Layer::endShape(bool closed) {
    if (!inShape_) return;
    inShape_ = false;
    Cmd& c = push(closed ? Kind::Polygon : Kind::Polyline);
    c.pts = shape_;
    c.closed = closed;
    shape_.clear();
}

// 所有记下来的图元的世界坐标包围盒。dot() 只算中心（半径是像素，量不出世界范围）；
// text() 只算锚点（字形大小要量字体才知道，Layer 不该为了这个背上字体依赖）。
Rect Layer::bounds() const {
    std::vector<Vec2> pts;
    auto              add = [&](const Vec2& p) { pts.push_back(p); };
    auto              addR = [&](const Vec2& c, double r) {
        pts.push_back(c - Vec2{r, r});
        pts.push_back(c + Vec2{r, r});
    };
    for (const Cmd& c : cmds_) {
        switch (c.kind) {
            case Kind::Line:     add(c.p0); add(c.p1); break;
            case Kind::Polyline:
            case Kind::Polygon:  for (const Vec2& p : c.pts) add(p); break;
            case Kind::Circle:   addR(c.p0, c.r1); break;
            case Kind::Ellipse:  addR(c.p0, std::max(c.r1, c.r2)); break;
            case Kind::Dot:      add(c.p0); break;
            case Kind::Rect:     add(c.box.min()); add(c.box.max()); break;
            case Kind::Triangle: add(c.p0); add(c.p1); add(c.p2); break;
            case Kind::Text:     add(c.p0); break;
            case Kind::Image:
            case Kind::ImageSub: add(c.box.min()); add(c.box.max()); break;
            case Kind::Arc:      addR(c.p0, c.r1); break;
            case Kind::Bezier:   add(c.p0); add(c.p1); add(c.p2); add(c.p3); break;
        }
    }
    return Rect::bounding(pts);
}

}  // namespace easel
