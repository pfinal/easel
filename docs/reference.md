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
    app.title("MySketch").size(1280, 800).theme(easel::Theme::Forest());
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
| `idleThrottle` | `App& idleThrottle(bool)` | 默认 `false`。开启后：无输入且闲置 > 0.5s 时，帧间隔放大到 100ms；一有输入/子进程运行中/toast 显示中立即恢复 `frameRate()` 速度 |
| `busyWhen` | `App& busyWhen(std::function<bool()>)` | `idleThrottle` 开启时，返回 `true` 则视为忙碌，保持满帧 |

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
| `onKey(fn(int))` | 某键刚按下的那一帧 | `ImGuiKey` |
| `onWindow(fn(const Rect&))` | 每帧，把画布区域交给自定义 ImGui 绘制 | 该区域的屏幕矩形 |

`onStart` 是**唯一**能调用 [`loadTexture`](#loadtexture) 的地方（`run()` 之前显卡上下文不存在），只调一次。
`onWindow` 可与 `onDraw` 同时使用，画在其上层。

---

## 2. 画布与坐标

- 位置、半径、矩形：世界坐标；线宽、字号：屏幕像素。
- 世界坐标系 y 轴向下（同屏幕/Scratch/Processing）。

<a id="坐标"></a>
```cpp
Vec2 s = c.toScreen(worldPoint);   // 世界 -> 屏幕像素
Vec2 w = c.toWorld(screenPoint);   // 屏幕像素 -> 世界
Rect visible = c.world();          // 当前画布可见的世界范围
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
void fit(const Rect& worldRect, double paddingPx = 40);   // 把该区域整个装入画布；run() 前调用也有效（延迟到首帧执行）
void center(const Vec2& w);
void zoom(double pixelsPerUnit);
void panZoom(bool enabled);                                // 关闭滚轮缩放/拖拽平移
void scaleBar(double* metersPerUnit, const char* unitName = "米");   // 传指针，改变量值比例尺即时更新
```

`app.camera()` 获取当前实例。默认交互：滚轮缩放（以鼠标为中心）、中键或空格+左键拖拽平移。

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
void rect(const Rect& r);   // Rect(x, y, w, h)；填充/描边取决于当前 fill/stroke 状态
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
// 角度为弧度；pie=true 连回圆心画成扇形（填充），否则只画弧线（不填充）
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
    float radius;                   // 圆角
    float fontSize;
    std::string fontPath;           // 空 = 自动找系统字体，见字体来源
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
```

`at` 为世界坐标，字号为屏幕像素（不随缩放变化）。

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
    void line/polyline/circle/ellipse/dot/rect/triangle/polygon/text/image(...);   // 同 Canvas 签名
    void   clear();                     // 全部擦除
    size_t size() const;                // 当前记录的图元条数
    void   limit(size_t maxCommands);   // 上限，默认 20 万，超出自动丢弃最早的
};
```

记录世界坐标的绘制指令，每帧由 `Canvas::draw(layer)` 重放；用于拖尾/涂鸦/图章等
"留下笔迹"的效果。作为 `App` 的成员变量持有，不要每帧新建。

    struct S { Layer ink; } S;
    void onFrame(double) { if (c.hovered()) S.ink.stroke(color, 2).line(prev, c.mouse()); }
    void onDraw(Canvas& c) { c.draw(S.ink); }

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
    Mouse button = Mouse::Left;
    bool  began = false, ended = false;   // 本帧是否为拖拽的首帧/末帧
};
App& onDrag(std::function<void(const Drag&)>);   // 拖拽中每帧调用
```

<a id="onkey"></a>
```cpp
App& onKey(std::function<void(int)>);   // 参数为 ImGuiKey；仅在键刚按下的那一帧触发一次
```

持续按住需在 `onFrame` 中自行判断：`ImGui::IsKeyDown(ImGuiKey_Space)`。

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
bool toggle(const char* label, bool* v);
bool button(const char* label, bool wide = false);
```

`v` 为唯一真相源，双向绑定（拖动改 `*v`，代码改 `*v` 滑块位置同步）。返回值 `true` 表示本帧发生变化。

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
Vec2   normalized() const;   Vec2 perp() const;      // 垂直向左（世界 y 向下时视觉上是左转）
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
};
Rng&     rng();            // 全局随机源
void     seed(unsigned);   // 同时重设 noise() 的种子；只想单独换 noise 用 noiseSeed(x)
unsigned current_seed();
```

不带 `--seed` 启动时每次换新种子，并打印当前种子（`--seed N` 复现）。

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
    void load(std::vector<T> frames);
    void push(const T&);
    void clear();
    const T& current() const;
    const T& at(int i) const;
    std::vector<T>& frames();
    size_t size() const;
};
// 播放控制（与 T 无关）：
void play(); void pause(); void toggle(); void step(int dir); void seek(int i);
void speed(double s); void fps(double f); void rewind(); bool loop;

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

std::string file::open(const std::string& filterName = "工程文件", const std::string& extensions = "json");
// 系统对话框；用户取消返回空串
std::string file::save(const std::string& defaultName = "project.json", const std::string& filterName = "", const std::string& extensions = "");
std::string file::folder(const char* defaultPath = nullptr);
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
| `--frames N` | 跑 N 帧自动退出（CI / 截图用） |
| `--screenshot <png>` | 退出前存一张截图 |
| `--fps N` | 帧率上限（`0` = 不限制），覆盖代码里 `frameRate()` |

自己的参数用 `cli::args()`（见[第 14 节](#cli-args)）。Windows 上作品是 GUI 程序，
从命令行运行时输出会接回当前终端。

### 主程序的命令行

上面那张表是**作品**（学生编译出来的 app）的命令行；工作台本身（`easel` /
`easel.exe`，双击打开的那个程序）另有一套无头命令行，不开窗口，CI、脚本、
老师批量操作用：

| 命令 | 作用 | 例子 |
|---|---|---|
| `easel --new <目录> --name <名字> [--full \| --example <名字>] [--tests]` | 从模板建一个新工程 | `easel --new ~/projects --name Demo --full` |
| `easel --build <工程目录> [--json]` | 只配置 + 编译 app 目标，不运行 | `easel --build ~/projects/Demo` |
| `easel --run <工程目录> [--args "..."] [--json]` | 编译后运行作品，等它退出；`--args` 把参数转给作品 | `easel --run ~/projects/Demo --args "--frames 30"` |
| `easel --package <工程目录> [--json]` | Release 构建 + 收进 `dist/<名字>-release/` | `easel --package ~/projects/Demo` |
| `easel --export <工程目录> --out <目录> [--json]` | 导出一个能独立编译的完整工程 | `easel --export ~/projects/Demo --out ~/out` |

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

## 18. 常见错误

**画布一片空白** → F12 → 画布页看图元数是不是 0：`onDraw` 是否提前 `return`；
若图元数不为 0 但「视口外」数量很大，多半是没调 `camera().fit(...)`。

**中文变成方框** → 没找到中文字体。F12 → 自检看字体来源；设
`Theme::fontPath = "assets/fonts/xxx.ttf"`，或设环境变量 `EASEL_FONT`。

**点了画布没反应** → 鼠标在面板上时不触发 `onClick`/`onDrag`；空格+拖拽是平移不是点击。

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

Easel · 代码酷 daimaku.net · MIT · 对应 API 冻结前的 v0.1.1（开发中）
