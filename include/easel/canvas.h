// Easel — canvas.h  画布：世界坐标里画东西
//
// 约定（和地图软件一样）：
//   位置、半径、矩形 —— 世界坐标（米、格子，你自己定）
//   线宽、字号       —— 屏幕像素（放大缩小时不会变粗、变大）
//   y 轴向下 —— 和屏幕、Scratch、Processing 一样；往上画就用负数
// 想要「不随缩放变大的圆点」用 dot()，想要「真实大小的圆」用 circle()。
//
// 变换栈（Processing 的 pushMatrix/translate/rotate/scale）：
//   c.push().translate(p).rotate(a);  c.rect(box);  c.pop();
//   push() 同时存样式和矩阵，pop() 一起还原。变换只影响「画出去的点」，
//   mouse() 和 toWorld() 不受影响 —— 鼠标永远是真正的世界坐标。
//
// 文字有个反直觉的地方：text() 只有锚点过变换矩阵，字形本身永远轴对齐、永远是屏幕
// 像素大小——旋转/缩放对它「看不见」。image() 恰恰相反，四个角都过矩阵，会跟着转/缩。
// 同一个变换栈下两种图元行为相反，别想当然。想要字跟着转/缩，用 textWorld()
// （字号是世界单位，逐字符贴图，四个角都过矩阵）。详见 text()/textWorld() 的注释。
//
// Layer（Scratch 的画笔）：画一次就留在那儿的笔迹。
//   Layer ink;                                   // 成员变量，不要每帧新建
//   void onFrame(App& a) {  ink.stroke(Color::hex(0x4CAF50), 2).line(prev, now);  }
//   void onDraw(Canvas& c) { c.draw(ink); }      // 每帧重放一遍
//   // 想「全部擦除」就 ink.clear()
// Layer 默认记的是绝对世界坐标，不认 Canvas 当前的变换栈（push/rotate 对它没用）。
// 想让落笔也跟着当前变换走，调 ink.follow(c)（记下 c 此刻的矩阵）；
// 用完记得 ink.noFollow() 切回绝对坐标，否则会一直按同一份矩阵记下去。
#ifndef EASEL_CANVAS_H
#define EASEL_CANVAS_H

#include <easel/camera.h>
#include <easel/core.h>

#include <deque>

namespace easel {

enum class Align { Left, Center, Right };

// 一张图片。用 easel::loadTexture("data/map.png") 得到。
struct Texture {
    std::uint64_t id = 0;
    int           w = 0, h = 0;
    std::string   path;
    explicit operator bool() const { return id != 0; }
    double aspect() const { return h > 0 ? (double)w / (double)h : 1.0; }
};

// 加载 png / jpg / bmp。失败返回一个空 Texture（if (!tex) 判断）。
// 只能在 App 跑起来之后调（要先有渲染后端）。
// pixelated：像素风（精灵图、马赛克）传 true——放大时保持一格一格的硬边，不模糊；
// 默认 false（照片、地图之类连续色调的图，线性插值放大更平滑）。
Texture loadTexture(const std::string& path, bool pixelated = false);
void    freeTexture(Texture& t);

class Layer;

class Canvas {
public:
    // ---------------- 坐标 ----------------
    Vec2    toScreen(const Vec2& w) const;
    Vec2    toWorld(const Vec2& s) const;
    Rect    world() const;                  // 当前可见的世界范围
    Rect    screen() const;                 // 画布在窗口里的像素矩形
    double  zoom() const;                   // 一个世界单位 = 多少像素
    Vec2    mouse() const;                  // 鼠标的世界坐标（不受变换栈影响）
    bool    hovered() const;                // 鼠标是否在画布上
    Camera& camera() const;

    // ---------------- 样式（状态式，像 Processing）----------------
    Canvas& fill(const Color& c);
    Canvas& noFill();
    Canvas& stroke(const Color& c, double widthPx);
    Canvas& stroke(const Color& c);
    Canvas& noStroke();
    Canvas& strokeWidth(double px);          // 只改线宽
    Canvas& alpha(double a);                // 0..1，乘在后面所有颜色上
    Canvas& textSize(double px);            // 字号（像素）
    Canvas& dashed(double onPx, double offPx);   // 虚线（对比模式的「现状路线」）
    Canvas& solid();
    Canvas& push();                         // 存一份样式 + 矩阵
    Canvas& pop();                          // 还原

    // ---------------- 变换栈（Processing 的 translate/rotate/scale）----------------
    // 后调用的变换先作用在点上：translate(p).rotate(a) 表示「先转，再挪过去」。
    // 记得配对 push()/pop()，不然变换会一直累积到下一次画东西上。
    Canvas& translate(const Vec2& d);       // 平移（世界单位）
    Canvas& rotate(double radians);         // 旋转，弧度；正角度在屏幕上是顺时针（y 轴向下，和 Processing 一样）
    Canvas& scale(double s);                // 等比缩放
    Canvas& scale(double sx, double sy);    // 分轴缩放（sx 为负就是水平翻转）
    Canvas& resetMatrix();                  // 清成单位阵
    Vec2    transform(const Vec2& p) const; // 这个点经过当前变换后落在哪（还是世界坐标）

    // ---------------- 图元 ----------------
    void line(const Vec2& a, const Vec2& b);
    void polyline(const std::vector<Vec2>& pts, bool closed = false);
    void circle(const Vec2& c, double radiusWorld);
    void dot(const Vec2& c, double radiusPx = 5.0);       // 大小不随缩放变化
    void rect(const Rect& r);
    // 字号是屏幕像素，不随缩放变化；旋转/缩放对字形「看不见」——只有 at 这个锚点
    // 过变换矩阵，字形本身永远轴对齐地画出来（ImGui 画不了旋转的 AddText）。
    // image() 相反，四个角都过矩阵，会跟着转。想要字跟着 rotate()/scale() 转/缩，
    // 用下面的 textWorld()。地图类作品（字号不该随缩放变大）就用这个 text()。
    void text(const Vec2& at, const std::string& s, Align align = Align::Left);
    // 当前 textSize() 下这段文字有多宽（逻辑像素，和 textSize() 的单位一致，不随
    // DPI 变化）。没有它就没法排版——想把一串文字右对齐/居中到某个位置、或者算一个
    // 刚好包住文字的按钮宽度，都得先知道文字有多宽。
    double textWidth(const std::string& s) const;
    // 字号是世界单位（sizeWorld），字形跟随当前变换——旋转、缩放都跟着转/缩，和
    // image() 一致。实现是逐字符四边形贴图（每个字一次 AddImageQuad），比 text() 贵
    // 不少，别拿它画成千上万个标签。想要「转圈的名字」之类效果用它；
    // 想要「字号固定、不随缩放变大」（地图标注）用 text()。
    void textWorld(const Vec2& at, const std::string& s, double sizeWorld, Align align = Align::Left);
    // textWorld() 的配套测量：给定世界单位字号 sizeWorld，这段文字画出来有多宽
    // （世界单位）。多行文字（含 '\n'）取最长一行的宽度。
    double textWidthWorld(const std::string& s, double sizeWorld) const;
    void image(const Texture& t, const Rect& worldRect);
    // 只画图片的一小块（Scratch 的「造型」）：srcPx 是源图上的像素矩形。
    // 精灵表逐帧动画：image(sheet, box, Rect(frame * 32, 0, 32, 32));
    void image(const Texture& t, const Rect& worldRect, const Rect& srcPx);

    // ---------------- 更多形状（Processing 同名函数）----------------
    void triangle(const Vec2& a, const Vec2& b, const Vec2& c);
    void polygon(const std::vector<Vec2>& pts);          // 凹的也画得对
    void ellipse(const Vec2& c, double rxWorld, double ryWorld);
    // 圆弧，弧度。pie = true 画扇形（连回圆心），否则只画一段弧。
    void arc(const Vec2& c, double radiusWorld, double a0, double a1, bool pie = false);
    void bezier(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3);

    // ---------------- 自定义形状（Processing 的 beginShape/vertex/endShape）----------------
    //     c.beginShape();  for (Vec2 p : pts) c.vertex(p);  c.endShape();
    void beginShape();
    void vertex(const Vec2& p);
    void endShape(bool closed = true);      // closed 就填充成多边形，否则只是折线

    // ---------------- 画笔图层 ----------------
    void draw(const Layer& layer);          // 把 Layer 记下的笔迹重放一遍

    // ---------------- 画布诊断用（F12 -> 画布）----------------
    int primitives() const { return stats_.primitives; }
    int offscreen() const { return stats_.offscreen; }

    // ---------------- Easel 内部 ----------------
    // Style/Stats 和 begin/end/setStyle/style() 技术上是内部实现，但 Layer 重放
    // （Canvas::draw）和 App 主循环要用到，所以保持公开而不是塞进 private+friend。
    struct Style {
        Color  fillColor = Color::hex(0x4CAF50);
        Color  strokeColor = Color::hex(0xE8EAED);
        bool   hasFill = true, hasStroke = true;
        double strokeWidth = 1.5;
        double alpha = 1.0;
        double fontPx = 15.0;
        double dashOn = 0, dashOff = 0;
    };
    struct Stats {
        int primitives = 0, offscreen = 0, invisible = 0;
        int lines = 0, circles = 0, rects = 0, texts = 0, images = 0, polygons = 0;
    };
    void         begin(Camera& cam, void* drawList, bool hovered, float dpiScale);
    void         end();
    const Stats& stats() const { return stats_; }
    // 给 Layer 重放用：整份样式一起换。
    const Style& style() const { return st_; }
    void         setStyle(const Style& s) { st_ = s; }

private:
    friend class Layer;   // Layer::follow() 要读 mat_ 这份矩阵，仅此一家（B6）

    Color applyAlpha(const Color& c) const;
    void  strokeSegment(const Vec2& sa, const Vec2& sb);   // 屏幕坐标，处理虚线
    Vec2  project(const Vec2& w) const;                    // 世界点 -> 过矩阵 -> 屏幕像素
    void  fillScreenPoly(const std::vector<Vec2>& sp, bool convex);
    void  strokeScreenPoly(const std::vector<Vec2>& sp, bool closed);

    // 2×3 仿射矩阵：x' = a x + c y + e，y' = b x + d y + f。只有 Canvas 自己的
    // 变换栈实现和 Layer::follow() 用得到，不是学生要打交道的类型，所以是 private（B6）。
    struct Mat {
        double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
        bool identity() const { return a == 1 && b == 0 && c == 0 && d == 1 && e == 0 && f == 0; }
        Vec2 apply(const Vec2& p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
        // m * n：先用 n 变换点，再用 m。Processing 的「后调用的先作用」就是靠这个顺序。
        static Mat mul(const Mat& m, const Mat& n) {
            return {m.a * n.a + m.c * n.b, m.b * n.a + m.d * n.b,
                    m.a * n.c + m.c * n.d, m.b * n.c + m.d * n.d,
                    m.a * n.e + m.c * n.f + m.e, m.b * n.e + m.d * n.f + m.f};
        }
    };
    struct Saved {                          // push() 存的一份现场
        Style st;
        Mat   mat;
    };
    Camera* cam_ = nullptr;
    float   dpi_ = 1.f;
    void*   dl_ = nullptr;
    bool    hovered_ = false;
    Style   st_;
    Mat     mat_;
    std::vector<Saved> stack_;
    std::vector<Vec2>  shape_;              // beginShape 攒的点
    bool               inShape_ = false;
    Stats              stats_;
};

// ============================================================================
//  Layer —— Scratch 的画笔
//  画一次就留在那儿的笔迹：拖尾、涂鸦、图章、走过的路。
//  它记的是「命令」，每帧由 Canvas::draw() 重放 ——
//  Easel 每帧都重画整张画布（相机缩放和回放才成立），所以「不清」这件事
//  必须由你自己记着，Layer 就是替你记的那个本子。
//
//      Layer ink;                                  // App 的成员，别放在 onDraw 里
//      ink.stroke(Color::hex(0xFFC107), 2);        // 样式和 Canvas 一模一样
//      ink.line(a, b);                             // 落笔
//      c.draw(ink);                                // onDraw 里重放
//      ink.clear();                                // 全部擦除
//
//  默认记的是绝对世界坐标，不认 Canvas 当前的变换栈——先 follow(c) 再落笔，
//  记下来的点就会按 c 此刻的矩阵变换过；不 follow() 就是老行为（绝对坐标）。
//
//  支持的图元：line/polyline/circle/ellipse/dot/rect/triangle/polygon/text/
//  image/arc/bezier/beginShape+vertex+endShape —— 和 Canvas 一样全，一个不少。
//  （没有 textWorld()：Layer 里的 text() 和 Canvas::text() 同样只有锚点跟变换，
//  字形不转；要「跟着转的字」现场用 c.textWorld() 画，别指望 Layer 存住它。）
//
//  默认最多记 20 万条，超了自动丢最早的（limit() 可以改）。
// ============================================================================
class Layer {
public:
    // ---------------- 样式（和 Canvas 同名同义，可以串起来写）----------------
    Layer& fill(const Color& c) { st_.fillColor = c; st_.hasFill = true; return *this; }
    Layer& noFill() { st_.hasFill = false; return *this; }
    Layer& stroke(const Color& c, double widthPx) {
        st_.strokeColor = c; st_.strokeWidth = widthPx; st_.hasStroke = true; return *this;
    }
    Layer& stroke(const Color& c) { st_.strokeColor = c; st_.hasStroke = true; return *this; }
    Layer& noStroke() { st_.hasStroke = false; return *this; }
    Layer& strokeWidth(double px) { st_.strokeWidth = px; return *this; }
    Layer& alpha(double a) { st_.alpha = clamp(a, 0.0, 1.0); return *this; }
    Layer& textSize(double px) { st_.fontPx = px; return *this; }

    // ---------------- 变换来源 ----------------
    // 把接下来记录的图元按 c 当前的变换矩阵（push/translate/rotate/scale 累积的那份）
    // 变换后再存；只在调用这一刻拍一张矩阵快照，之后 c 再变不会追着改已经记过的点。
    //     c.push().rotate(a); ink.follow(c).line(p1, p2); c.pop();   // 落笔时按 a 转好
    // 用完切回 noFollow()（默认状态），否则后面所有落笔都会一直按这份矩阵变换。
    Layer& follow(const Canvas& c) { xf_ = c.mat_; return *this; }
    Layer& noFollow() { xf_ = Canvas::Mat{}; return *this; }

    // ---------------- 图元 ----------------
    void line(const Vec2& a, const Vec2& b);
    void polyline(const std::vector<Vec2>& pts, bool closed = false);
    void circle(const Vec2& c, double radiusWorld);
    void ellipse(const Vec2& c, double rxWorld, double ryWorld);
    void dot(const Vec2& c, double radiusPx = 5.0);
    void rect(const Rect& r);
    void triangle(const Vec2& a, const Vec2& b, const Vec2& c);
    void polygon(const std::vector<Vec2>& pts);
    void text(const Vec2& at, const std::string& s, Align align = Align::Left);
    void image(const Texture& t, const Rect& worldRect);
    void image(const Texture& t, const Rect& worldRect, const Rect& srcPx);
    void arc(const Vec2& c, double radiusWorld, double a0, double a1, bool pie = false);
    void bezier(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3);
    void beginShape();
    void vertex(const Vec2& p);
    void endShape(bool closed = true);

    // ---------------- 管理 ----------------
    void   clear() { cmds_.clear(); }               // Scratch 的「全部擦除」
    size_t size() const { return cmds_.size(); }    // 现在记了多少条
    void   limit(size_t maxCommands);               // 改上限，超出的最早那些立刻丢掉
    size_t limit() const { return limit_; }
    // 所有记下来的图元的世界坐标包围盒（比如喂给 camera.fit()）；没记过东西返回一个
    // empty() 的 Rect。dot() 的半径是像素、text() 只有锚点，这两类只按中心点/锚点算，
    // 不是精确的视觉范围。
    Rect bounds() const;

    const Canvas::Style& style() const { return st_; }

private:
    friend class Canvas;   // Canvas::draw 要重放 commands()/Kind/Cmd，仅此一家（B6）

    double followScale() const { return std::sqrt(std::fabs(xf_.a * xf_.d - xf_.b * xf_.c)); }
    double followRotation() const { return std::atan2(xf_.b, xf_.a); }
    // worldRect 的四角过 xf_；没有旋转/斜切时结果仍是精确的轴对齐矩形，
    // 有旋转时 Image 命令存不了斜的四边形，退化成包围盒（image() 转不起来这一点
    // 记在类注释里了——真要转的贴图用 c.image() 现场画，别走 follow() 过的 Layer）。
    Rect followedBox(const Rect& r) const;

    // ---------------- Easel 内部（只有 Canvas::draw 用）----------------
    enum class Kind { Line, Polyline, Circle, Ellipse, Dot, Rect, Triangle, Polygon, Text, Image, ImageSub,
                       Arc, Bezier };
    struct Cmd {
        Kind          kind = Kind::Line;
        Canvas::Style st;
        Vec2          p0, p1, p2, p3;
        double        r1 = 0, r2 = 0, a0 = 0, a1 = 0;
        Rect          box, src;
        std::vector<Vec2> pts;
        std::string       txt;
        Texture           tex;
        Align             align = Align::Left;
        bool              closed = false;
    };
    const std::deque<Cmd>& commands() const { return cmds_; }

    Cmd& push(Kind k);                              // 加一条命令，顺手裁掉超出上限的

    Canvas::Style   st_;
    std::deque<Cmd> cmds_;
    size_t          limit_ = 200000;
    Canvas::Mat     xf_{};                  // follow()/noFollow() 设的「录入变换」，默认单位阵
    std::vector<Vec2> shape_;               // beginShape 攒的点（已经过 xf_）
    bool               inShape_ = false;
};

// ============================================================================
//  Graphics —— 离屏画布（一块自己的位图，画上去就留着）
//
//  Layer 靠「记下命令、每帧重放」积累笔迹，量一大（几万条）就重放不动了。
//  Graphics 是一块真正的显卡画布（GPU 的 PGraphics）：画一次，像素就烧在里面，
//  下一帧不用重画，O(1)。流场、涂鸦、长拖尾、粒子留痕这类"越画越多"的效果用它。
//
//      Graphics g;
//      app.onStart([&]{ g.create(1280, 800); });
//      app.onFrame([&](double dt){
//          g.begin();                          // 之后的绘制都进这块缓冲
//          g.canvas().stroke(c, 2).line(a, b);
//          g.end();
//      });
//      app.onDraw([&](Canvas& c){ c.image(g.texture(), worldRect); });   // 整块贴到主画布
//
//  坐标系：g.canvas() 用的是**像素坐标**——左上角 (0,0)，右下角 (w,h)，1 个单位
//  = 1 个像素，不受主画布相机（缩放/平移）影响。「往缓冲里画」的含义就此固定：
//  你写的每个坐标就是缓冲里的那个像素。想把世界坐标的东西画进缓冲（比如让流场
//  跟着主画布一起缩放），自己用主画布的相机换算一遍：
//
//      Vec2 px = mainCanvas.toScreen(worldPos) - mainCanvas.screen().min();  // 世界 -> 缓冲像素
//
//  和 Layer 的分工：Layer 记的是矢量命令，能跟着相机缩放/平移，量要小（几千条）；
//  Graphics 是定尺寸的位图，缩放会糊，但量可以很大（几十万次绘制/帧不掉帧）。
//  两者不互斥，一个作品里可以都用：Layer 画少量还能交互的笔迹，Graphics 画背景的
//  大量积累效果。
//
//  时机：begin() 只能在 onStart / onFrame 里调（那时主画布还没在录制）；onDraw
//  里主画布正在录制，调 begin() 会报 EASEL_CHECK 错。begin() 之后必须 end()，
//  没 end() 就再 begin() 也会报错。
// ============================================================================
class Graphics {
public:
    Graphics() = default;
    ~Graphics();
    Graphics(const Graphics&) = delete;
    Graphics& operator=(const Graphics&) = delete;
    Graphics(Graphics&& o) noexcept;
    Graphics& operator=(Graphics&& o) noexcept;

    // 像素尺寸。失败（没有渲染后端 / 显卡不支持 / 尺寸非法）返回 false 并 EASEL_WARN，
    // 不崩、也不留下半初始化的状态。可以多次调（先 destroy() 旧的再建新的，比如换尺寸）。
    bool create(int w, int h);
    void destroy();
    bool valid() const { return fboId_ != 0 && texId_ != 0; }
    int  width() const { return w_; }
    int  height() const { return h_; }

    // 整块清掉（不清 = 上一帧的内容原样留着，这正是"积累"的关键）。
    void clear(const Color& c = Color(0, 0, 0, 0));

    // 开始往这块缓冲画：之后所有 canvas() 上的绘制都进这块缓冲，直到 end()。
    // 只能在 onStart / onFrame 里调；onDraw 里调、或者没 end() 就再 begin() 都是用错了，
    // EASEL_CHECK 报错但不崩（横幅一提示，回放照样能继续看）。
    Canvas& begin();
    Canvas& canvas() { return canvas_; }   // begin()/end() 之间也能拿到，方便串写
    void    end();

    // 拿去 c.image(g.texture(), worldRect) 贴到主画布上。这个 Texture 不拥有资源——
    // Graphics 销毁时才真正释放显卡资源，**别**对它调 freeTexture()。
    Texture texture() const;

private:
    int           w_ = 0, h_ = 0;
    std::uint64_t texId_ = 0;
    std::uint64_t fboId_ = 0;
    Camera        cam_;
    Canvas        canvas_;
    void*         dl_ = nullptr;    // ImDrawList*（这里只存指针，别让公开头文件依赖 imgui.h）
    bool          active_ = false;  // begin() 了还没 end()
};

}  // namespace easel
#endif
