# Easel API 参考

Easel 的全部公开 API：签名、参数、返回值与关键行为约定。只讲"是什么/怎么用"，
不讲"为什么这样设计"——设计理由、项目惯例、与 Scratch/Processing/openFrameworks
的对照见 [guide.md](guide.md)。一页纸速查表见 [cheatsheet.md](cheatsheet.md)。

签名以 `include/easel/*.h` 为准。单头版 `easel.hpp`（命令行程序用）与多头版
`<easel/easel.h>`（界面程序用）是同一份代码的两种发行形式，二选一。

---

## 1. App 与生命周期

<a id="app-结构"></a>
### `App`

```cpp
int main(int argc, char** argv) {
    easel::App app(argc, argv);
    app.title("MySketch").size(800, 450).theme(easel::Theme::Forest());   // size 可以不写，默认就是 800x450
    app.onDraw([](easel::Canvas& c) { ... });
    app.onPanel([] { ... });
    return app.run();
}
```

构造函数自动完成 `cli::parse`（之后 `cli::args()` 可用）和崩溃处理器安装。
以下设置项均返回 `App&`，可链式调用：

| 方法 | 签名 | 说明 |
|---|---|---|
| `title` | `App& title(const std::string&)` | 窗口标题 |
| `size` | `App& size(int w, int h)` | 初始窗口尺寸 |
| `theme` | `App& theme(const Theme&)` | 见 [`Theme`](#theme) |
| `statusBar` | `App& statusBar(bool)` | 底部状态栏，默认 `false`；F12 打开调试台时临时显示 |
| `frameRate` | `App& frameRate(double fps)` | 帧率上限，默认 60；传 `0` 关闭上限。命令行 `--fps N` 之后覆盖。窗口最小化/被遮挡时自动降到约 10 帧 |
| `maxDelta` | `App& maxDelta(double seconds)` | `onFrame`/拖拽等算出来的 `dt` 的上限，默认 `0.05`（20fps 一帧）。窗口被挡一下再切回来，现实里过去的时间可能是好几秒，不夹住粒子/物理模拟会一帧瞬移到很远；传 `0` = 不夹 |
| `idleThrottle` | `App& idleThrottle(bool)` | 默认 `false`。开启后：无输入且闲置 > 0.5s 时，帧间隔放大到 100ms；一有输入/子进程运行中/toast 显示中立即恢复 `frameRate()` 速度 |
| `busyWhen` | `App& busyWhen(std::function<bool()>)` | `idleThrottle` 开启时，返回 `true` 则视为忙碌，保持满帧 |
| `background` | `App& background(const Color&)` | 画布底色，不设就用主题的 `bg` |
| `panelWidth` | `App& panelWidth(float px)` | 右侧面板宽度 |
| `editorEnabled` | `App& editorEnabled(bool)` | `false` = 连 F9 都不响应。工具类作品建议关掉 |
| `debugConsoleEnabled` | `App& debugConsoleEnabled(bool)` | `false` = 连 F12 都不响应 |

其它常用成员：

```cpp
App& welcome(const std::string& title, const std::string& desc, std::function<void()> body);
void closeWelcome();  bool welcomeOpen() const;   // 启动页：别让人一进来看到空白画布
void toast(const std::string& s);                 // 顶部中间飘一条，两秒半后消失
void status(const std::string& s);                // 底部状态栏文字（需 statusBar(true)）
bool screenshot(const std::string& path);         // 把当前画面存成 PNG（见下面的注意）
long long frameCount() const;                     // 现在是第几帧（Processing 的 frameCount）
double elapsed() const;                           // 从启动到现在多少秒（Processing 的 millis()，单位是秒）
Camera& camera();  Canvas& canvas();
double dpiScale() const;  const char* backendName() const;
void quit();
static App* instance();
```

`screenshot()` 在 `onDraw`/`onPanel` 里手动调时，读到的是**上一次呈现**的画面（这一帧
还没画完、还没交换缓冲区），也就是慢一两帧。要"所见即所得"就用命令行的
`--frames N --screenshot x.png`——那条路的截图卡在"画完、还没交换"的那一刻，存的
一定是最后那一帧本身。

**帧号与运行时长**——作品里想知道"现在是第几帧""跑了多久"，直接问 `app`，不用自己
在 State 里加计数器，也不用自己 `new` 一个 `Stopwatch`：

```cpp
app.onDraw([&](Canvas& c) {
    if (app.frameCount() % 30 == 0) ...;                  // 每半秒（60 帧）换一次
    double t = app.elapsed();                              // 秒
    c.circle({std::cos(t) * 100, std::sin(t) * 100}, 20);  // 转圈
});
```

| 成员 | 类型 | 说明 |
|---|---|---|
| `app.frameCount()` | `long long` | 第一帧的回调里就是 `1`，之后每帧 +1。`--warmup N` 预热的那 N 帧**也算**（预热的就是"帧"，否则靠帧号驱动的动画会完全感觉不到 `--warmup`）；`--frames N` 数的是**画出来**的帧，所以有 `--warmup` 时两者差一个 N。F12 调试台里的「第 N 帧」、日志行首的 `fN` 都是同一个数 |
| `app.elapsed()` | `double` | 秒，真实时钟。同一帧里多次调用返回同一个值（帧开头取一次，整帧沿用），所以 `onFrame` 和 `onDraw` 算出来的动画相位不会错开。`--warmup` **不**推进它——预热是一瞬间跑完的 |

只读，没有对应的 setter：想要"自己的计时/计数"就自己留一个变量，框架这两个数永远
如实反映真实的帧和时间。

<a id="onframe"></a>
### 回调

| 回调 | 触发时机 | 参数 |
|---|---|---|
| `onFrame(fn(double dt))` | 每帧最先调用 | `dt`：帧间隔（秒） |
| `onStart(fn())` | 窗口和显卡就绪后，只调一次 | 无 |
| `onDraw(fn(Canvas&))` | 每帧，画布区域 | 画布 |
| `onPanel(fn())` | 每帧，右侧面板区域 | 无 |
| `onClick(fn(Vec2, Mouse))` | 点击画布 | 世界坐标、按下的键 |
| `onDrag(fn(const Drag&))` | 拖拽中的每一帧 | 见 [`Drag`](#ondrag) |
| `onKey(fn(Key))` | 某键刚按下的那一帧 | [`Key`](#key) |
| `onWindow(fn(const Rect&))` | 每帧，把画布区域交给自定义 ImGui 绘制 | 该区域的屏幕矩形 |

`onStart` 是**唯一**能调用 [`loadTexture`](#loadtexture) 的地方（`run()` 之前显卡上下文不存在），只调一次。
`onWindow` 可与 `onDraw` 同时使用，画在其上层。

<a id="什么时候能调什么"></a>
### 什么时候能调什么

| 时机 | 能做 | 不能做 |
|---|---|---|
| **全局变量的初始化** | 纯计算 | **任何 easel 调用**。`audio::Sound::load` 之类会碰到 Easel 内部的全局对象，而全局之间的初始化顺序是未定义的——今天能跑，换台机器/换个链接顺序就崩在 `main` 之前，最难查 |
| `main` 里、`run()` 之前 | `camera().fit()`/`center()`/`zoom()`（会延迟到首帧执行）、`cli::args()`、各种设置项 | `loadTexture`（没有显卡上下文） |
| `onStart` | `loadTexture`、`audio::Sound::load`、`audio::loop`——**资源都在这里加载** | — |
| `onFrame` | 改状态、跑模拟 | 这里读 `c.hovered()`/`c.mouse()` 拿到的是**上一帧**的值（画布本帧还没 begin） |
| `onDraw` / `onPanel` | 画图、读当帧的 `c.mouse()`/`c.hovered()` | — |

一句话：**资源加载放 `onStart`，别放全局变量的初始化。**

---

## 2. 画布与坐标

- 位置、半径、矩形：世界坐标；线宽、字号、圆角半径：屏幕像素。
- 世界坐标系 y 轴**向下**：和屏幕像素、Processing / p5.js / openFrameworks / HTML canvas
  一致。**和 Scratch 不一样**——Scratch 的 y 轴向上、原点在舞台正中、范围是
  x ±240、y ±180（180 是最上面）。从 Scratch 过来的话最直接的换算是把 y 取反：
  Scratch 的 `(x, y)` 在这里写成 `{x, -y}`。
- **一次 `fit()` 都不调的时候，画布看到的是什么**：世界原点 `{0, 0}` 在画布正中，
  1 个世界单位 = 1 个屏幕像素（相机默认 `center={0,0}`、`zoom=1`）。也就是说
  `c.circle({0, 0}, 50)` 是正中一个半径 50 像素的圆，`{100, 0}` 在它右边 100 像素，
  `{0, 100}` 在它**下面** 100 像素。想按自己的数据范围取景就调一次
  `app.camera().fit(...)`（见下面 [`Camera`](#camera)）。

<a id="坐标"></a>
```cpp
Vec2 s = c.toScreen(worldPoint);   // 世界 -> 屏幕像素
Vec2 w = c.toWorld(screenPoint);   // 屏幕像素 -> 世界
Rect visible = c.world();          // 当前画布可见的世界范围
Rect box = c.screen();             // 画布在窗口里的像素矩形；做"贴在屏幕角落"的 HUD 时用
                                   //   Vec2 hud = c.toWorld(c.screen().min() + Vec2(14, 14));
double z = c.zoom();               // 一个世界单位 = 多少像素
Vec2 m = c.mouse();                // 鼠标当前世界坐标；不受变换栈（push/translate/rotate）影响
```

<a id="c-hovered"></a>
```cpp
bool c.hovered();   // 鼠标是否在画布区域内（不含右侧面板）；onClick/onDrag 仅在为 true 时触发
```

<a id="camera"></a>
### `Camera`

```cpp
void fit(const Rect& worldRect, double paddingPx = 0);    // 把该区域整个装入画布，默认严格贴合；run() 前调用也有效（延迟到首帧执行）
                                                            // 画面里有贴边的屏幕像素图元（dot() 半径、text() 字号、粗线线宽）时，
                                                            // 世界坐标包围盒量不到它们，严格贴合会切掉一截，这时传个像素留白
void center(const Vec2& w);
void zoom(double pixelsPerUnit);
void panZoom(bool enabled);                                // 滚轮缩放/拖拽平移，默认关
void scaleBar(double* metersPerUnit, const char* unitName = "米");   // 传指针，改变量值比例尺即时更新
```

`app.camera()` 获取当前实例；在 `onDraw` 里手边只有画布时，`c.camera()` 拿到的是同一个。
滚轮缩放（以鼠标为中心）、中键或空格+左键拖拽平移，**默认关着**——多数作品有固定构图，
滚一下就把画面缩没了。要浏览大世界的作品调 `panZoom(true)` 打开。

---

## 3. 形状

位置为世界坐标，线宽/字号为屏幕像素。

<a id="c-line"></a>
```cpp
void line(const Vec2& a, const Vec2& b);
void polyline(const std::vector<Vec2>& pts, bool closed = false);   // closed=true 首尾相连
```

<a id="c-rect"></a>
```cpp
void rect(const Rect& r, double roundingPx = 0);
// Rect(x, y, w, h)；填充/描边取决于当前 fill/stroke 状态。
// roundingPx 是圆角半径，单位**屏幕像素**（和 strokeWidth 同类，不随缩放变大）；
// 0 = 直角。超过短边一半会被夹到一半（正方形给个大值就是一个圆）。
//     c.fill(th.accent); c.rect(Rect(-60, -40, 120, 80), 12);   // 圆角卡片
// 限制：当前**有变换**时（push().translate()/rotate()/scale() 之后），矩形不再轴对齐，
// 走的是四边形那条绘制路径，**圆角会被忽略**，画出来是直角。需要圆角就别在旋转状态
// 下画，或者自己用 polygon() 铺四个角。
```

<a id="c-circle-dot"></a>
```cpp
void circle(const Vec2& center, double radiusWorld);    // 半径为世界单位，随缩放变化
void dot(const Vec2& center, double radiusPx = 5);       // 半径为屏幕像素，不随缩放变化，用于标记位置
```

<a id="c-ellipse"></a>
```cpp
void ellipse(const Vec2& center, double rxWorld, double ryWorld);   // 传半轴（非直径），均为世界单位
```

<a id="c-arc"></a>
```cpp
void arc(const Vec2& center, double radius, double a0, double a1, bool pie = false);
// 角度为弧度；pie=true 把弧的两端连回圆心，画成一块扇形。
// 注意：**填不填充只看当前 fill 状态，与 pie 无关**——fill 开着时，pie=false 的弧
// 也会被两端的弦封起来填成一片。想要一条干净的弧线（比如笑脸的嘴），先 noFill()。
```

<a id="c-triangle-polygon"></a>
```cpp
void triangle(const Vec2& a, const Vec2& b, const Vec2& c);
void polygon(const std::vector<Vec2>& pts);   // 支持凹多边形
```

<a id="c-bezier"></a>
```cpp
void bezier(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3);
// p0/p3 端点，p1/p2 控制点
```

<a id="c-beginshape"></a>
```cpp
void beginShape();
void vertex(const Vec2& p);
void endShape(bool closed = true);   // closed=true 填充成多边形，false 只画折线
```

---

## 4. 颜色与样式

<a id="color"></a>
### `Color`

```cpp
Color Color::hex(uint32_t rgb, float alpha = 1.f);       // 如 0x2E7D32
Color Color::rgb(int r, int g, int b, int a = 255);      // 分量 0..255
Color Color::hsv(double h, double s, double v, float alpha = 1.f);   // h: 0..360，s/v: 0..1
Color Color::gray(float g, float alpha = 1.f);
Color Color::withAlpha(float a) const;
Color Color::lighter(float t) const;
Color Color::darker(float t) const;
Color Color::mix(const Color& other, float t) const;
```

内部分量为 `float`，范围 0..1；`rgb()`/`hex()` 接受常见写法并换算。

<a id="样式"></a>
### 样式状态

```cpp
Canvas& fill(const Color&);
Canvas& noFill();
Canvas& stroke(const Color&, double widthPx);
Canvas& stroke(const Color&);       // 仅改颜色，线宽不变
Canvas& noStroke();
Canvas& strokeWidth(double px);     // 仅改线宽
Canvas& alpha(double a);            // 0..1，乘在后续所有颜色上
Canvas& dashed(double onPx, double offPx);
Canvas& solid();                    // 取消虚线
Canvas& push();                     // 保存样式 + 变换矩阵
Canvas& pop();                      // 还原
```

状态式：设置后对之后绘制的所有图元生效，直到下次修改。链式调用。

<a id="theme"></a>
### `Theme`

```cpp
struct Theme {
    Color accent, accent2;          // 主色 / 次色
    Color bg, surface, fg, muted;   // 画布背景 / 面板 / 正文 / 次要文字
    Color good, warn, bad;          // 语义色：成功 / 警告 / 危险
    bool  dark;                     // 深色主题为 true
    float radius;                   // 圆角
    float fontSize;
    std::string fontPath;           // 空 = 自动找系统字体，见字体来源
    std::string name;               // 主题名，如 "Forest"；做主题切换 UI 时用它当标签
};
Theme Theme::Forest();  Theme Theme::Ocean();  Theme Theme::Ember();   // 深色
Theme Theme::Paper();   Theme Theme::Slate();                          // 浅色
std::vector<Theme> Theme::presets();
```

---

## 5. 变换

<a id="c-translate"></a>
```cpp
Canvas& translate(const Vec2& d);   // 世界单位
```

<a id="c-rotate"></a>
```cpp
Canvas& rotate(double radians);   // 绕当前原点，正角度为顺时针（y 轴向下）
```

<a id="c-scale"></a>
```cpp
Canvas& scale(double s);
Canvas& scale(double sx, double sy);   // sx 为负 = 水平翻转
```

<a id="c-push-pop"></a>
```cpp
Canvas& push();          // 保存样式 + 矩阵
Canvas& pop();           // 还原
Canvas& resetMatrix();   // 矩阵重置为单位阵（不影响样式）
Vec2    transform(const Vec2& p) const;   // p 经当前变换后的世界坐标
```

调用顺序即作用顺序：`translate(p).rotate(a)` = 先转再移。`c.mouse()`/`c.toWorld()`
不受变换栈影响。未配对的 `push()`/`pop()` 会导致变换累积到下一次绘制。

<a id="变换影响谁"></a>
### 变换影响谁、不影响谁

第 2 节讲的是**单位**（世界坐标还是屏幕像素），这里讲的是另一件事：**哪些东西会被
`rotate` / `scale` 改变**。两者不是一回事，混起来是最容易踩的坑。

| | 跟着变换走 | 不跟着 |
|---|---|---|
| 位置 | 所有图元的顶点；`image()` 的四个角 | — |
| 尺寸 | `circle`/`ellipse`/`arc` 的半径、`rect` 的边长 | 线宽、字号、`dot()` 的半径 |
| 文字 | `textWorld()`：锚点**和字形**都跟 | `text()`：只有**锚点**，字形永远轴对齐、永远是屏幕像素 |
| 鼠标 | — | `c.mouse()`、`c.toWorld()` |

两个最常见的意外：

- **`text()` 不会转、也不会缩。** 对画在局部原点的一行字做 `rotate()` / `scale()`，
  锚点没动，字形又不受矩阵影响，屏幕上**什么变化都没有**（代码看着完全正确）。
  想让字变大改 `textSize()`；想让字跟着转/缩，用 [`textWorld()`](#c-textworld)——
  字号是世界单位，逐字符贴图，四个角都过矩阵，和 `image()` 一样会转。
- **`image()` 会转，`text()` 不会。** 贴图是四个角分别过矩阵的，所以 `rotate`/`scale`
  能把图片转起来、翻过来；`text()` 却只挪锚点。同一个变换栈下两种图元行为相反，
  别套用——`textWorld()` 才是和 `image()` 同一套行为的文字版本。

线宽同理：想让线条越来越细（比如递归画树的枝条），不能靠 `scale()`，
必须每一级显式 `stroke(color, width)` 或 `strokeWidth(px)` 传新值。

---

## 6. 图片

<a id="loadtexture"></a>
```cpp
Texture loadTexture(const std::string& path, bool pixelated = false);
void    freeTexture(Texture& t);
```

支持 png/jpg/bmp；失败返回空 `Texture`（`if (!tex)` 可判断）。**只能在 `onStart` 中调用**。
`pixelated=true` 用于像素风素材（放大保持硬边）；默认 `false` 为线性插值（适合照片/连续色调图）。

<a id="c-image"></a>
```cpp
void image(const Texture& t, const Rect& worldRect);                    // 整张图铺满 worldRect
void image(const Texture& t, const Rect& worldRect, const Rect& srcPx); // 只画源图上 srcPx 像素矩形（精灵表逐帧动画）
```

---

## 7. 文字

<a id="c-text"></a>
```cpp
enum class Align { Left, Center, Right };
Canvas& textSize(double px);   // 状态式，屏幕像素
void    text(const Vec2& at, const std::string& s, Align align = Align::Left);
double  textWidth(const std::string& s) const;   // 当前 textSize() 下这段文字多宽（逻辑像素）
```

`at` 为世界坐标，字号为屏幕像素（不随缩放变化）。`textWidth()` 不画东西，只测量——
排版（右对齐、居中到某个位置、算一个刚好包住文字的按钮宽度）离不开它，单位和
`textSize()` 一致，不用自己再乘 DPI。

**文字不会跟着 `rotate()` / `scale()` 转或缩**——只有锚点过变换矩阵，字形本身永远
轴对齐地画出来。要变大改 `textSize()`；要绕圈就 `translate` 把锚点推出去再 `rotate`。
详见[变换影响谁、不影响谁](#变换影响谁)。

<a id="c-textworld"></a>
```cpp
void   textWorld(const Vec2& at, const std::string& s, double sizeWorld, Align align = Align::Left);
double textWidthWorld(const std::string& s, double sizeWorld) const;   // textWorld() 的配套测量，世界单位
```

`text()` 的反面：`sizeWorld` 是世界单位，字形**跟随当前变换**——`rotate()`/`scale()`
都跟着转/缩，和 `image()` 一致。想要「转圈的名字」「贴在旋转物体上的标签」这类效果
用它；想要「字号固定、不随缩放变大」（地图标注、UI 文字）还是用 `text()`。

实现是逐字符四边形贴图（每个字一次 `AddImageQuad`），比 `text()` 贵不少——用它测量、
排版一大段文字之前先想想[性能预算](#性能预算)，不要拿它画成千上万个标签。

<a id="字体来源"></a>
### 中文字体查找顺序

`Theme::fontPath`（代码指定）→ 环境变量 `EASEL_FONT` → `assets/fonts/` 内置子集 →
系统字体（Mac 苹方/冬青黑 GB，Windows 微软雅黑/黑体，Linux Noto Sans CJK）→
均无则用 ImGui 内置字体（中文变方框，`--doctor` 会说明原因）。

---

## 8. 画笔（`Layer`）

<a id="layer"></a>
```cpp
class Layer {
    Layer& fill(const Color&);  Layer& stroke(const Color&, double px);   // 同 Canvas
    // 下面这些的签名与 Canvas 的同名函数一致，图元集合和 Canvas 一样全：
    void line/polyline/circle/ellipse/dot/rect/triangle/polygon/text/image(...);
    void arc/bezier(...);
    void beginShape();  void vertex(const Vec2&);  void endShape(bool closed = true);
    // 没有 textWorld()：Layer 里的 text() 和 Canvas::text() 一样只有锚点跟变换，
    // 字形不转；要「跟着转的字」现场用 c.textWorld() 画，Layer 存不住那个。
    // rect() 也没有圆角参数（只有 Canvas::rect 有），Layer 里的矩形一律是直角。

    Layer& follow(const Canvas& c);   // 接下来落的笔按 c 此刻的变换矩阵变换后再存
    Layer& noFollow();                // 切回绝对世界坐标（默认状态）

    void   clear();                     // 全部擦除
    size_t size() const;                // 当前记录的图元条数
    void   limit(size_t maxCommands);   // 上限，默认 20 万，超出自动丢弃最早的
    Rect   bounds() const;              // 所有记下来的图元的世界坐标包围盒
};
```

记录绘制指令，每帧由 `Canvas::draw(layer)` 重放；用于拖尾/涂鸦/图章等
"留下笔迹"的效果。作为 `App` 的成员变量持有，不要每帧新建。

    struct State { Layer ink; } state;
    void onFrame(double) { if (c.hovered()) state.ink.stroke(color, 2).line(prev, c.mouse()); }
    void onDraw(Canvas& c) { c.draw(state.ink); }

几条要点：

- **默认记的是绝对世界坐标，不认 Canvas 当前的变换栈。** 落笔时 Canvas 上的
  `push`/`rotate` 对它没有影响；反过来，`c.push().rotate(a); c.draw(ink); c.pop();`
  会把**整个图层**一起转，不是把每一笔各转一份。
- **想让落笔也跟着当前变换走，用 `follow(c)`。** 它只在调用那一刻拍一张 `c` 的
  矩阵快照，之后 `c` 再变不会追着改已经记过的点：

      c.push().rotate(a); ink.follow(c).line(p1, p2); c.pop();   // 落笔时就转好了

  用完记得 `noFollow()` 切回默认状态，否则后面所有落笔都会一直按同一份矩阵变换。
  「一笔自动复制成 N 份旋转副本」（万花筒）现在可以在落笔的循环里 `follow(c)`
  配合 `c.rotate()` 直接写，不必再手算 `Vec2::rotated()`。
  `circle`/`ellipse`/`arc` 在有旋转/非等比缩放的 `follow()` 下按统一缩放因子近似
  （和 Canvas 自己遇到非单位阵时退化成多边形是同一个道理）；`image()` 在有旋转的
  `follow()` 下只能退化成包围盒摆放（贴图本身不会转）——真要转的贴图用 `c.image()`
  现场画，别指望走 `follow()` 过的 Layer。
- **`limit()` 要按重放成本定，不是按内存定。** `Canvas::draw(layer)` 每帧把记下的
  每一条命令重走一遍，且**不做视口裁剪**。默认上限 20 万对累积类作品偏大，
  见[性能预算](#性能预算)。

---

## 9. 交互

<a id="onclick"></a>
```cpp
enum class Mouse { Left, Right, Middle };
App& onClick(std::function<void(Vec2, Mouse)>);   // 世界坐标 + 按键；鼠标在面板上时不触发
```

<a id="ondrag"></a>
```cpp
struct Drag {
    Vec2  start, current, delta;   // 世界坐标；delta 为本帧位移（非累计总量）
    Vec2  velocity;                // 世界单位/秒；ended 帧也有效，见下
    Mouse button = Mouse::Left;
    bool  began = false, ended = false;   // 本帧是否为拖拽的首帧/末帧
};
App& onDrag(std::function<void(const Drag&)>);   // 拖拽中每帧调用
```

`velocity` 是最近几帧的平滑速度，"松手甩出去"（惯性抛出、缩放/滚动的惯性）直接读
`ended` 帧的 `velocity` 就行，不用像 `delta` 那样自己去接上一帧的值。

<a id="key"></a>
```cpp
enum class Key {
    A, B, ..., Z,                                   // 字母
    Num0, Num1, ..., Num9,                           // 数字（不能用 0/1/2 当标识符开头）
    Left, Right, Up, Down,                           // 方向键
    Tab, Space, Enter, Escape, Backspace, Delete,    // 常用控制键
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,
    F1, F2, ..., F12,                                // 功能键
};
```

`Key` 的数值就是对应的 `ImGuiKey`（编译期 `static_assert` 保证对齐），所以这里没
列出的冷门键（小键盘、F13 往上……）依然可以强转塞进来用：`(Key)ImGuiKey_KeypadEnter`。
鼠标键不在 `Key` 里，那是 [`Mouse`](#onclick) 的地盘。

<a id="onkey"></a>
```cpp
App& onKey(std::function<void(Key)>);   // 仅在键刚按下的那一帧触发一次
bool keyDown(Key k) const;              // 这一帧是否按住；Canvas 上也有一份同名的，转调这个
```

两者不重复：**按下瞬间**（比如「空格开火」）用 `onKey`；**持续按住**（比如「按住方向键
移动」）用 `keyDown`，一般在 `onFrame`/`onDraw` 里每帧判断：

```cpp
app.onFrame([&](double dt) {
    if (app.keyDown(Key::Left))  x -= speed * dt;
    if (app.keyDown(Key::Right)) x += speed * dt;
});
// 或者在 onDraw 里，拿着 Canvas& 直接用，不用捕获 app：
app.onDraw([&](Canvas& c) {
    if (c.keyDown(Key::Up))   y -= 2;   // 每帧挪一点，简单场景不需要单独算 dt
    if (c.keyDown(Key::Down)) y += 2;
});
```

<a id="click-drag-契约"></a>
### `onClick` 与 `onDrag` 的关系

- **两者互斥，同一次按下松开只会触发其中一个。** 松手时：若已经进入拖拽状态，
  只发一次 `onDrag`（`ended = true`）；若从按下到松开位移小于 4 像素，只发 `onClick`。
  所以"点一下生成一个、拖一下甩出去"不会一次生出两个。
- **`ended` 那一帧的 `delta` 是 `{0, 0}`，但 `velocity` 不是。** 想拿拖拽速度当
  初速度（"甩出去"），直接用 `ended` 帧的 `Drag::velocity`——它记的是松手前最近
  几帧的平滑速度，不会因为松手前恰好停顿一帧就读成 0，不用再像以前那样自己去接
  上一帧的 `delta`。
- `delta` 是**本帧位移**，不是从起点算起的累计量；累计量自己用 `current - start`。

---

## 10. 面板控件（`ui::`）

<a id="面板控件-ui"></a>
`#include <easel/easel.h>` 已带 `<imgui.h>`/`<implot.h>`，未覆盖的控件可直接混用 `ImGui::XXX`。

<a id="ui-slider"></a>
```cpp
bool section(const char* label, bool defaultOpen = true);   // 折叠分组；返回值决定是否绘制内部控件
bool slider(const char* label, double* v, double lo, double hi, const char* fmt = "%.2f");
bool slider(const char* label, float*  v, float  lo, float  hi, const char* fmt = "%.2f");
bool slider(const char* label, int*    v, int    lo, int    hi);
// 和 slider 外观、参数完全一样，但只在**松手那一帧**返回 true（拖动过程中的每一帧
// 都是 false）。参数改动会触发重算（重新跑一遍模拟/优化）时用它，别用 slider——
// 否则拖一下、中间几十帧全部重算一遍，重的作品会卡成幻灯片。
bool sliderCommit(const char* label, double* v, double lo, double hi, const char* fmt = "%.2f");
bool sliderCommit(const char* label, float*  v, float  lo, float  hi, const char* fmt = "%.2f");
bool sliderCommit(const char* label, int*    v, int    lo, int    hi);
bool toggle(const char* label, bool* v);
bool button(const char* label, bool wide = false);
```

`v` 为唯一真相源，双向绑定（拖动改 `*v`，代码改 `*v` 滑块位置同步）。返回值 `true` 表示本帧发生变化。

<a id="ui-select"></a>
```cpp
bool select(const char* label, int* v, std::initializer_list<const char*> items);
bool select(const char* label, int* v, const std::vector<std::string>& items);
```
下拉框（N 选一）。`*v` 是选中项的下标，返回值同 `slider`：`true` 表示这一帧选项变了。
```cpp
if (ui::select("算法", &state.algo, {"冒泡排序", "选择排序"})) sortAll();
```
选项数量固定用 `initializer_list` 这版最短；选项是运行时算出来的（文件名列表等）用 `vector<string>` 这版。

<a id="ui-input"></a>
```cpp
bool input(const char* label, std::string* v);
bool inputCommit(const char* label, std::string* v);
```
单行文字输入。`v` 是唯一真相源，内部自己管缓冲区——不用像 `ImGui::InputText` 那样自己开
`char buf[N]`、传 `sizeof buf`，字符串长度也不再被 `N` 卡死。
`input` 只要这一帧内容变了（敲一个字符、删一个字符……）就返回 `true`，跟 `slider` 拖动
中每帧都 `true` 是一个道理，适合「打字的时候画面就跟着变」：
```cpp
if (ui::input("要显示的文字", &state.text)) { /* 文字随打随变，通常什么都不用做 */ }
```
要是这行字只是用来触发一次性的动作（重新生成、重新计算），每敲一个字符都触发一遍
既没意义又浪费，这时候用 `inputCommit`——和 `sliderCommit` 对 `slider` 的关系一样，
只在敲完回车或失焦离开那一刻返回 `true`，其余帧都是 `false`：
```cpp
if (ui::inputCommit("种子", &state.seedText))
    state.mon = makeMonster((unsigned)std::strtoul(state.seedText.c_str(), nullptr, 10));
```

<a id="ui-stat-chart"></a>
```cpp
void stat(const char* label, const std::string& value, const char* unit = "");
void stat(const char* label, double value, const char* unit = "", int decimals = 2);
void stat(const char* label, int value, const char* unit = "");
void chart(const char* label, const std::vector<double>& ys, const char* seriesName = "值", float height = 140.f);
void chart(const char* label, const std::vector<double>& xs, const std::vector<double>& ys, const char* seriesName = "值", float height = 140.f);
```

```cpp
void separator(); void spacing(); void sameLine();
void title(const char* text);
void help(const char* text);   // 灰色说明文字，自动换行
```

---

## 11. 数学与随机

<a id="vec2"></a>
### `Vec2`

```cpp
struct Vec2 { double x, y; };
Vec2   operator+(Vec2, Vec2);   Vec2 operator-(Vec2, Vec2);
Vec2   operator*(Vec2, double); Vec2 operator/(Vec2, double);   Vec2 operator-(Vec2);
double length() const;    double length2() const;   // 长度平方，省一次开方
Vec2   normalized() const;   Vec2 perp() const;      // 逆时针 90°：(x, y) -> (-y, x)
double angle() const;     Vec2 rotated(double rad) const;
```

<a id="rect"></a>
### `Rect`

```cpp
struct Rect { double x, y, w, h; };   // x/y 为左上角
Rect   Rect::fromCorners(Vec2 a, Vec2 b);
Rect   Rect::fromCenter(Vec2 c, double w, double h);
Rect   Rect::bounding(const std::vector<Vec2>& pts);
double left()/right()/top()/bottom() const;
Vec2   min()/max()/center() const;
bool   empty() const;
bool   contains(const Vec2& p) const;
bool   overlaps(const Rect& other) const;
Rect   expanded(double px) const;
Rect   united(const Rect& other) const;
```

<a id="remap-lerp-clamp"></a>
### 常用函数

```cpp
double dist(Vec2 a, Vec2 b);     double dist2(Vec2 a, Vec2 b);
double dot(Vec2 a, Vec2 b);      double cross(Vec2 a, Vec2 b);
Vec2   lerp(Vec2 a, Vec2 b, double t);   double lerp(double a, double b, double t);
double clamp(double v, double lo, double hi);
double remap(double v, double lo, double hi, double lo2, double hi2);   // 不夹紧，超出范围结果也超出
```

<a id="kpi"></a>
```cpp
inline constexpr double kPi  = 3.14159265358979323846;   // 对应 Processing 的 PI
inline constexpr double kTau = 6.28318530717958647692;   // 对应 Processing 的 TWO_PI（2π，转一整圈）
double radians(double deg);   // 对应 Processing 的 radians()
double degrees(double rad);   // 对应 Processing 的 degrees()
```

MSVC 的 `<cmath>` 不带 `M_PI`（那是 POSIX 扩展）；不用再自己 `constexpr double kPi = 3.14159...`。

<a id="rng"></a>
### `Rng`

```cpp
class Rng {
    int    i(int lo, int hi);            // 闭区间随机整数
    double d(double lo = 0, double hi = 1);
    bool   chance(double p);
    double normal(double mean = 0, double sd = 1);
    template<class T> void shuffle(std::vector<T>& v);
    template<class T> const T& pick(const std::vector<T>& v);
    void     reseed(unsigned s);   // 只重设这一个 Rng，不动 noise
    unsigned seed() const;
};
Rng&     rng();            // 全局随机源
void     seed(unsigned);   // 同时重设 noise() 的种子；只想单独换 noise 用 noiseSeed(x)
unsigned current_seed();
```

不带 `--seed` 启动时每次换新种子，并打印当前种子（`--seed N` 复现）。

**想让"同一颗种子 = 同一个结果"真的成立，一行里别抽两次。** C++ 不规定同一个
表达式里几个函数调用谁先执行，所以 `rng().pick(A) + rng().pick(B)` 这样写，
换个编译器就可能抽出不同结果。拆成两条语句：

```cpp
const std::string& a = rng().pick(A);
std::string name = a + rng().pick(B);
```

<a id="noise"></a>
### `noise`

```cpp
double noise(double x);
double noise(double x, double y);
double noise(double x, double y, double z);
void   noiseSeed(unsigned s);
void   noiseDetail(int octaves, double falloff = 0.5);   // octaves: 1~8
```

返回值范围 `[0,1]`；与 `rng()` 不同，相邻输入产生相邻输出（连续渐变，非跳变噪点）。

<a id="stopwatch-bench"></a>
### `Stopwatch` / `bench::run`

```cpp
class Stopwatch { void reset(); double ms() const; double s() const; };
template<class F> bench::Result bench::run(F&& f, int repeats = 5);   // .str() 打印统计结果
```

---

## 12. 声音

<a id="audio-play"></a>
```cpp
bool play(const std::string& path);   // wav/mp3/flac；失败（路径不对/无声卡）返回 false，不崩
bool loop(const std::string& path);
void stop();
void volume(double v);   double volume();   // 总音量 0..1
```

<a id="audio-loudness-spectrum"></a>
```cpp
double loudness();                            // 当前播放输出的响度 0..1（非麦克风采集）
std::vector<float> spectrum(int bands = 64);  // 低频到高频，已做时间平滑
```

<a id="audio-sound"></a>
```cpp
class Sound {
    static Sound load(const std::string& path);
    void play();  void stop();  void volume(double v);  void pitch(double p);   // 1.0 = 原速原调
    bool loop = false;                     // 下次 play() 生效
    explicit operator bool() const;        // 加载失败/无声卡 -> false
};
```

拷贝共享同一份声音（内部引用相同资源）；同一时刻只播放一次，重复 `play()` 从头重放。
无声卡时 `audio::ok()` 为 `false`，所有 `audio::` 调用静默失败；`audio::doctor()` 说明原因。

---

## 13. 回放

<a id="timeline"></a>
### `Timeline<T>` / `App::transport`

```cpp
template<class T> class Timeline {
    void load(std::vector<T> frames);   // 载入后 pos 归 0 且**处于暂停**，要放得自己再 play()
    void push(const T&);
    void clear();
    bool empty() const;
    const T& current() const;
    const T& at(int i) const;           // i 自动夹到 [0, size-1]，越界不会崩
    std::vector<T>& frames();           // 另有 const 版
    size_t size() const;
};
// 播放控制（与 T 无关，在基类 TimelineBase 上）：
void play(); void pause(); void toggle(); void step(int dir = 1); void seek(int i);
void speed(double s); void fps(double f); void rewind(); bool loop;
int    index() const;      // 当前第几帧——画 current() 之外的东西（轨迹、进度）时几乎必用
bool   playing() const;
bool   atEnd() const;
double progress() const;   // 0..1

App& transport(TimelineBase&);   // 接到窗口底部播放条：进度条/倍速/单步
```

约定：`solve()` 为纯函数，一次性把全过程采样为帧序列返回；界面只负责回放。
算法耗时应控制在几百毫秒内，更大规模只在文档中报告离线结果。

---

## 14. 数据与文件

<a id="easel-json"></a>
### `EASEL_JSON`

```cpp
EASEL_JSON(Type, 字段...)          // 写在结构体外部，为公开成员生成 JSON 读写
EASEL_JSON_MEMBER(Type, 字段...)   // 写在结构体内部，用于私有成员
```

之后 `json j = obj; Type t = j.get<Type>();` 可用。

<a id="fs"></a><a id="file"></a>
### `fs::` / `file::`

```cpp
bool        fs::exists(const std::string& path);
bool        fs::makeDirs(const std::string& path);
std::string fs::readText(const std::string& path, bool* ok = nullptr);
bool        fs::writeText(const std::string& path, const std::string& content);
json        fs::loadJson(const std::string& path);            // 失败返回 json()（null）并打印 warn，不抛异常
bool        fs::saveJson(const std::string& path, const json& j, int indent = 2);

// 这三个收的都是 const char*（不是 std::string）；传 std::string 变量要 .c_str()
std::string file::open(const char* filterName = "工程文件", const char* extensions = "json");
// 系统对话框；用户取消返回空串
std::string file::save(const char* defaultName = "project.json", const char* filterName = "工程文件", const char* extensions = "json");
std::string file::folder(const char* defaultPath = nullptr);
// extensions 是过滤器规格：多个扩展名用逗号分隔，如 "wav,mp3,flac"
```

<a id="cli-args"></a>
### `cli::args()`

```cpp
bool        cli::args().has(const std::string& flag);
std::string cli::args().str(const std::string& name, const std::string& def = "");
long        cli::args().num(const std::string& name, long def = 0);
double      cli::args().real(const std::string& name, double def = 0.0);
std::string cli::args().at(size_t i, const std::string& def = "");   // 第 i 个不带 -- 的位置参数
```

`App` 构造时已调用 `cli::parse`；`EASEL_STANDALONE` 的 `main` 中需自行调用一次。

---

## 15. 调试

<a id="调试宏"></a>
### 宏

```cpp
EASEL_LOG(fmt, ...)          // 命令行打到终端；界面打到日志窗（带 file:line、帧号）
EASEL_WARN(fmt, ...)
EASEL_ERROR(fmt, ...)
EASEL_TRACE(name, value)     // 命令行输出一行 CSV；界面自动画成折线图
EASEL_CHECK(cond, msg)       // 命令行：假则打印调用栈并 abort；界面：假则弹红色横幅、暂停回放，不崩溃
EASEL_CHECK_EQ(a, b)         // 同上，附带打印两侧的值
EASEL_CHECK_NEAR(a, b, eps)  // 同上，允许 eps 误差
```

`cout`/`printf` 同样可用：命令行在终端看，界面里同时进日志窗。

<a id="dbg"></a>
### `easel::dbg`

```cpp
void easel::dbg(const char* key, T value);   // value 需可 << 到 ostream 或可转 json
```

在 F12 调试台「状态」页实时显示一行；命令行下为空操作。

<a id="调试相关"></a>
### F12 调试台

| 页 | 内容 |
|---|---|
| 日志 | 级别过滤、重复折叠、一键复制 |
| 追踪 | `EASEL_TRACE` 自动画成的折线图 |
| 状态 | `dbg()` 的实时表格 |
| 画布 | 图元数、视口外个数、看不见的个数、世界范围 |
| 用例 | 导出 + 复现命令 + `debug/` 目录列表 |
| 自检 | 后端、显卡、DPI、字体、种子、工作目录、编译器、帧率 |

状态栏最左侧的点：绿=正常，黄=有警告，红=断言失败或崩溃。F12 开关调试台。

```cpp
App& onExportCase(std::function<json()> fn);            // 未设置时导出按钮为灰
std::string exportDebugCase(const json& state, const std::string& note = {});
```

命令行侧读回：`debug::importCase(path)`。

### 对拍器

```cpp
auto rep = EASEL_CROSSCHECK(rounds, gen, fast, slow);
// gen(Rng&) -> Input；fast(Input)/slow(Input) -> Result（需支持 == 或可转 json）
// 不一致时存 debug/duipai-NNN.json；第 k 轮种子 = 起始种子 + k
```

### `--doctor`

```cpp
std::string doctorCore();          // 编译器/C++标准/平台/Debug或Release/ASan/种子/cwd
std::string App::doctor() const;   // + 渲染后端/字体/DPI
```

---

## 16. 命令行参数一览

App 内置的（构造 `App` 时自动解析）：

| 参数 | 作用 |
|---|---|
| `-h`, `--help` | 打印这张表（框架级参数）然后退出，不开窗口。作品自己定义的参数列不出来——框架不知道有哪些 |
| `--open <文件>` | 启动时打开它（`app.openPath()` 取） |
| `--solve` | 打开之后直接算（`app.wantsSolve()` 判断） |
| `--seed N` | 指定随机种子（不给则每次运行换新的，启动时打日志） |
| `--case <文件>` | 载入一个调试用例 |
| `--doctor` | 打印环境自检然后退出，不开窗口 |
| `--debug` | 启动就打开 F12 调试台 |
| `--edit` | 启动就打开 F9 编辑栏 |
| `--edit-run` | 打开编辑栏并立刻编译运行一次（CI 用） |
| `--export [目录]` | 导出可独立编译的完整工程，然后退出 |
| `--quiet` | `EASEL_TRACE`/`EASEL_LOG` 不往终端刷屏 |
| `--frames N` | 跑 N 帧自动退出（CI / 截图用）。**N 必须 ≥ 1**：`--frames 0` 是非法值，会报错并以退出码 2 退出，不会开窗口——想"一直跑"就别加这个参数（"不限制"是 `--fps 0` 的意思，别和它记混） |
| `--screenshot <png>` | 退出前存一张截图：存的是**最后那一帧画好的画面**（在交换缓冲区之前抓的），所以 `--frames 1 --screenshot x.png` 存的就是第一帧本身 |
| `--fps N` | 帧率上限（`0` = 不限制），覆盖代码里 `frameRate()` |
| `--warmup N` | 进入正常循环前先空转 N 帧（只调 `onFrame(dt)`，`dt` 固定 `1/60`，**不渲染**），再开始正常帧 |

自己的参数用 `cli::args()`（见[第 14 节](#cli-args)）。Windows 上作品是 GUI 程序，
从命令行运行时输出会接回当前终端。

**`--frames N --screenshot x.png` 截出来是空画面？** `--frames 120` 只跑了大约两秒，
凡是"要等几秒才发生一次"或"要积累一阵才好看"的效果，那时候都还没发生。加一个
`--warmup N`：`app --warmup 300 --frames 30 --screenshot x.png`，先用固定 `1/60`
的 `dt` 空转 300 帧「只跑逻辑不渲染」，再开始正常的 30 帧和截图——同一条命令永远
预热出同一个状态，可复现。写作品时也可以按"一打开就有画面"来设计：定时器的初值
直接设成大于阈值（让第一次立刻触发）。

### 主程序的命令行

上面那张表是**作品**（学生编译出来的 app）的命令行；工作台本身（`easel` /
`easel.exe`，双击打开的那个程序）另有一套无头命令行，不开窗口，CI、脚本、
老师批量操作用：

| 命令 | 作用 | 例子 |
|---|---|---|
| `easel --new <目录> --name <名字> [--full \| --example <名字>] [--tests]` | 从模板建一个新工程 | `easel --new ~/projects --name Demo --full` |
| `easel --build <工程目录> [--json]` | 只配置 + 编译 app 目标，不运行 | `easel --build ~/projects/Demo` |
| `easel --run <工程目录> [--args "..."] [--json]` | 编译后运行作品，等它退出；`--args` 把参数转给作品 | `easel --run ~/projects/Demo --args "--frames 30"` |
| `easel --package <工程目录> [--json]` | 导出应用程序：Release 构建 + 收进 `dist/<名字>-release/` | `easel --package ~/projects/Demo` |
| `easel --export <工程目录> --out <目录> [--json]` | 导出工程：一个能独立编译的完整工程 | `easel --export ~/projects/Demo --out ~/out` |
| `easel --clean <工程目录> [--json]` | 清理：删掉这个工程的构建产物（`.easel/build`、`.easel/build-release`）。**不动 `dist/`，不动你的代码**；目录本来就不在也算成功，可以连着跑两次 | `easel --clean ~/projects/Demo` |
| `easel -h` / `easel --help` | 打印工作台的完整用法 | `easel --help` |
| `easel -V` / `easel --version` | 打印版本、提交、abi、编译器 | `easel --version` |

工作台的参数表是固定的，所以它会**拒绝认不出的参数**：`easel --badarg` 打一行
「不认识的参数：--badarg」加完整用法，以退出码 2 退出，**不开窗口**。作品那边正好相反
——学生的作品会用 `cli::args().num("pad", 40)` 自定义参数，框架无从判断哪些名字合法，
所以作品只加 `--help`，绝不拒绝认不出的参数。

`--example` 写错名字时会把这份 Easel 里真正可用的示例都列出来
（扫的是 `<easel 目录>/examples` 下带 `main.cpp` 的子目录）。

**在 Easel 仓库自己的开发机上**（不是发布包/工具箱），`--new` 建的工程默认会从源码编
ImGui/GLFW（头一回 3 分钟）。先跑一次 `cmake --build build/default --target prebuilt`
（等价于 `cmake --install build/default --prefix build/default/prebuilt`，需要
`EASEL_INSTALL=ON`，顶层配置默认就是开的），装出 `build/default/prebuilt/`；
之后 `--new` 生成的工程会自动探测到它并链上预编译库，几秒钟编完，不用再等。

退出码：成功 0，失败 1；`--run` 编译失败时也是 1（作品没机会跑），编译成功后
退出码 = 作品自己的退出码。

`--json` 是给工具/脚本用的，不是给人读的：不带它就原样透传编译器/作品的输出，
末尾补一行人话摘要（`编译成功（4.2 秒）` / `编译失败：2 个错误、1 个警告`）；
带了 `--json` 就只往 stdout 打一条 JSON（其它信息一律进 stderr），带 `ok` /
`errors` / `warnings` / `diagnostics`（每条有 `file`/`line`/`column`/`message`）
等字段，`--run` 再多 `exitCode`/`stdout`，`--package` 多 `output`，`--export`
多 `output`/`checks`。

---

## 17. 快捷键一览

| 键 | 作用 |
|---|---|
| **F5** | 编辑栏内：保存并编译运行命令行版 `solver` |
| **F9** | 开/关 F9 编辑栏 |
| **F12** | 开/关调试台 |
| **Esc** | 关闭欢迎页 |
| 滚轮 | 缩放（以鼠标为中心） |
| 中键拖拽 | 平移画布 |
| 空格 + 左键拖拽 | 平移画布（无中键鼠标时用） |
| Ctrl + 滚轮 | 编辑栏里调字号 |
| Ctrl + S | 编辑栏里保存 |

---

## 18. 性能预算

<a id="性能预算"></a>
Easel 每帧重画整张画布（相机缩放和回放才能成立），所以**每帧的图元总数**是唯一
需要盯的指标。F12 →「画布」页显示的就是它。

几个定数量级用的事实：

- 一条 `line` 在底层是 6 个顶点。**几万条图元/帧是安全区**，十几万开始吃力，
  二三十万基本就是幻灯片了。
- `imconfig.h` 里 `ImDrawIdx` 默认是 **16 位**，单个绘制批次超过 65535 个顶点要靠
  后端的 `VtxOffset` 支持才不出错。这也是别把图元数堆太高的另一个理由。
- `Canvas::draw(Layer)` **逐条重放、不做视口裁剪**，每条还要存/取一次样式。
  Layer 的 `limit()` 该按这个定：累积类作品（流场、涂鸦、长拖尾）通常设到
  **几万条**就到头了，默认的 20 万只适合"其实画不了那么多"的场合。
- `text()` 比 `line()` 贵（要测量、要排字形），画成千上万个标签之前先想想。
- 视口外的图元**照样要走一遍**投影和判断，只是不出现在画面上。地图/无限世界
  这类作品要自己用 `c.world()` 把循环范围裁掉，并按 `c.zoom()` 动态调整格子大小，
  否则缩得越远画得越多。

一句话：**图元数要跟画布的像素面积挂钩，不能跟世界的大小挂钩。**

---

## 19. 常见错误

**画布一片空白** → F12 → 画布页看图元数是不是 0：`onDraw` 是否提前 `return`；
若图元数不为 0 但「视口外」数量很大，多半是没调 `camera().fit(...)`。

**中文变成方框** → 没找到中文字体。F12 → 自检看字体来源；设
`Theme::fontPath = "assets/fonts/xxx.ttf"`，或设环境变量 `EASEL_FONT`。

**点了画布没反应** → 鼠标在面板上时不触发 `onClick`/`onDrag`；空格+拖拽是平移不是点击。
另外这两个回调是**互斥**的，稍微拖一下就只会走 `onDrag` 不走 `onClick`，
见 [`onClick` 与 `onDrag` 的关系](#click-drag-契约)。

**加了 `rotate()` / `scale()`，文字纹丝不动** → 设计行为：`text()` 只有锚点过变换矩阵，
字形永远轴对齐。要变大用 `textSize()`，要绕圈先 `translate` 把锚点推出去；
要字形本身也转/缩，换 [`textWorld()`](#c-textworld)。见[变换影响谁、不影响谁](#变换影响谁)。

**`arc()` 画出来是实心的一片** → 填不填充只看 fill 状态，与 `pie` 无关。
想要一条弧线先 `noFill()`。

**画着画着越来越卡** → `Layer` 没设 `limit()`，或设得太大。`Canvas::draw(layer)`
每帧要把记下的每一条重放一遍且不裁剪，见[性能预算](#性能预算)。

**同一个 `--seed`，两台机器/两个编译器结果不一样** → 多半是在一个表达式里抽了两次随机数
（如 `rng().i(..) + rng().i(..)`），C++ 不规定谁先执行。拆成两条语句。

**程序在 `main` 之前就崩了 / 偶发崩溃换台机器才出现** → 在全局变量的初始化里调了
Easel 的东西（最常见是 `audio::Sound::load`）。全局之间的初始化顺序未定义，
资源加载一律放 `onStart`。见[什么时候能调什么](#什么时候能调什么)。

**`loadTexture` 返回空 `Texture` / 崩溃** → 只能在 `App::onStart` 中调用；
之前（全局变量初始化、`run()` 之前）显卡上下文还未建立。

**图片路径读不到** → 相对路径相对**当前工作目录**，非 exe 目录、非源码目录。
`--doctor` 打印当前工作目录，先用 `fs::exists(path)` 检查。

**贴图放大后一格一格发糊** → 像素风素材未传 `pixelated=true`：`loadTexture("sheet.png", true)`。

**改了 `solver.cpp`，`App` 里没变化** → 不是热重载，需重新构建对应 target。

**没声卡的机器上 `audio::` 什么都不响** → 设计行为，非 bug：`audio::ok()` 为 `false`，
所有调用静默失败；`audio::doctor()`/F12 自检可查具体原因。

**数组越界但程序没报错，直接算出错误结果** → Release 构建默认不做越界检查；
用 `cmake --preset debug`（ASan + 标准库调试模式）重新构建。

**崩溃了但调用栈看不出是哪一行** → 用 VS Code 的 F5（Debug 配置），或 `cmake --preset debug`。

---

## 20. 离屏画布（`Graphics`）

<a id="graphics"></a>

### 为什么要它——`Layer` 的上限

Easel 每帧重画整张画布（相机缩放、回放才能成立）。`Layer` 靠"记下命令、每帧
`Canvas::draw(layer)` 重放"实现"留下笔迹"，重放是**逐条**的，不做视口裁剪——
见[性能预算](#性能预算)。流场、涂鸦、长时间的拖尾、粒子留痕这类**积累型**创作，
命令数很容易冲到几十万条，实测 6 万条左右就开始掉帧，而这类作品往往要画到
几十万甚至更多。

`Graphics` 是一块真正的显卡位图（对应 Processing 的 `PGraphics`）：画一次，
像素就烧在里面，下一帧不用重画，复杂度是 **O(1)/帧**，跟你已经画了多少条完全
无关。用它取代 `Layer` 做积累型效果，`Layer` 留给"量小、还要跟着相机缩放"的
矢量笔迹。

### API

```cpp
class Graphics {
public:
    bool    create(int w, int h);      // 像素尺寸；失败返回 false 并 EASEL_WARN，不崩
    void    destroy();
    bool    valid() const;
    int     width() const;  int height() const;
    void    clear(const Color& c = Color(0, 0, 0, 0));   // 整块清掉
    Canvas& begin();                   // 开始往这块缓冲画；返回它自己的 Canvas
    Canvas& canvas();                  // begin()/end() 之间也能拿到，方便串写
    void    end();                     // 结束——这一批画的东西这时才真正烧进纹理
    Texture texture() const;           // 拿去 c.image(...) 贴到主画布上
};
```

禁止拷贝（`= delete`），允许移动。析构自动 `destroy()`。

```cpp
Graphics g;
app.onStart([&]{ g.create(1280, 800); });
app.onFrame([&](double dt){
    g.begin();                            // 之后的绘制都进这块缓冲
    g.canvas().stroke(c, 2).line(a, b);
    g.end();
});
app.onDraw([&](Canvas& c){ c.image(g.texture(), worldRect); });   // 整块贴到主画布
```

### 坐标系：像素，不是世界坐标

`g.canvas()` 用的是**像素坐标**——左上角 `(0,0)`，右下角 `(w,h)`，1 个单位 =
1 个像素，**不受主画布相机（缩放/平移）影响**。它有自己的一台相机，1:1、不缩放，
和主画布的 `Camera` 是两回事。这样"往缓冲里画"的含义是固定的：你写的每一个坐标
就是缓冲里的那个像素，不会因为主画布这一帧缩放了多少而变。

想把世界坐标的东西画进缓冲（比如让流场跟着主画布一起缩放平移），自己用主画布的
相机换算一遍，再喂给 `g.canvas()`：

```cpp
// 世界坐标 -> 主画布屏幕像素 -> 减去画布左上角 -> 缓冲像素
Vec2 toBufferPx(Canvas& mainCanvas, const Vec2& world) {
    return mainCanvas.toScreen(world) - mainCanvas.screen().min();
}
```

### 时机：`begin()`/`end()` 能在哪调

- 只能在 `onStart` / `onFrame` 里调——那时主画布还没在录制。
- **不能**在 `onDraw` 里调：那时主画布正在录制，`begin()` 会触发 `EASEL_CHECK`
  报错（红色横幅，不崩）。
- `begin()` 之后必须配对 `end()`；没 `end()` 就再 `begin()` 也会触发 `EASEL_CHECK`。
- `create()` 之前 `valid()` 是假的；`begin()`/`end()`/`clear()` 在没建成功时
  静默什么都不做，不崩——和 `loadTexture` 失败返回空 `Texture` 是一路的设计。

### 和 `Layer` 的分工

| | `Layer` | `Graphics` |
|---|---|---|
| 记的是 | 矢量命令（世界坐标） | 像素（GPU 位图） |
| 重放/积累代价 | 每帧逐条重放，随条数线性增长 | O(1)/帧，跟画了多少条无关 |
| 跟着主画布缩放/平移 | 会（`toScreen` 每帧重算） | 不会（贴图放大会糊） |
| 适合的量级 | 几千条，还要交互 | 几十万次绘制也不掉帧 |
| 典型场景 | 少量还能撤销/编辑的笔迹 | 流场、涂鸦、长拖尾、粒子留痕 |

两者不互斥：一个作品里可以都用——`Layer` 画少量还能交互的笔迹，`Graphics`
画背景的大量积累效果，`onDraw` 里先 `c.image(g.texture(), ...)` 再 `c.draw(layer)`
叠上去。

### 完整示例：流场

```cpp
struct State {
    Graphics           flow;
    std::vector<Vec2>  pts;
} state;

app.onStart([&]{
    state.flow.create(1000, 700);
    for (int i = 0; i < 4000; ++i) state.pts.push_back({rng().d(0, 1000), rng().d(0, 700)});
});

app.onFrame([&](double dt){
    Canvas& c = state.flow.begin();
    for (Vec2& p : state.pts) {
        double a = noise(p.x * 0.004, p.y * 0.004, state.t * 0.06) * 2 * kTau;
        Vec2   np = p + Vec2{std::cos(a), std::sin(a)} * 2.2;
        c.stroke(Color::hsv(a * 57.3, 0.6, 0.85, 0.5), 1.2).line(p, np);
        p = np;
    }
    state.flow.end();
});

app.onDraw([&](Canvas& c){
    c.camera().fit(Rect(0, 0, 1000, 700));
    c.image(state.flow.texture(), Rect(0, 0, 1000, 700));
});
```

完整可跑的版本见 `examples/creative/main.cpp` 的「流场」小节（`--part 4`）。

### 后端支持

- **OpenGL**：完整支持，真正的 FBO + 颜色贴图。
- **DX11**：`create()` 直接返回 `false` 并 `EASEL_WARN`——这是已知缺口（D3D11
  版需要一块 `ID3D11Texture2D` 渲染目标 + `ID3D11RenderTargetView`，工作量和
  GL 差不多，但目前没有 DX11 的实测环境，不实测不敢接）。DX11 后端下想要"积累"
  效果，退回用 `Layer`（重放开销更大，但至少画得对）。

### 常见错误

**`begin()`/`end()` 之间画的东西贴出来是空的** → 先检查 `g.valid()`：`create()`
是不是失败了（看有没有 `EASEL_WARN`）。DX11 后端下 `create()` 目前必定失败，见上。

**贴到主画布上是糊的** → `Graphics` 的像素尺寸是创建时定死的，放大贴出来和放大
任何一张贴图一样会糊；需要更清晰就 `create()` 一块更大的缓冲。

**在 `onDraw` 里调 `g.begin()` 报 `EASEL_CHECK` 红色横幅** → 设计行为，见
[离屏画布](#graphics) 一节的"时机"：`begin()`/`end()` 只能在 `onStart`/`onFrame` 里配对调用。

---

Easel · 代码酷 daimaku.net · MIT · 对应 API 冻结前的 v0.1.1（开发中）
