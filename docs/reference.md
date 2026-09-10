# Easel 参考手册

Easel 的全部公开 API，按用途分类。签名以 `include/easel/*.h` 为准。一页纸的速查表见 [cheatsheet.md](cheatsheet.md)。

---

## 0. 从熟悉的名字找起

Easel 把 Scratch、Processing、openFrameworks 三种写法接到同一个库上。
三张对照表：左列是 Scratch / Processing / openFrameworks 里的名字，右列是 Easel 里的写法。

### 对应 Scratch

| Scratch 积木 | Easel 里的写法 |
|---|---|
| 移动 10 步 / 面向 90 方向 | [`Vec2`](#vec2) 加减、[`c.rotate()`](#c-rotate) |
| 画笔：落笔 / 抬笔 / 全部擦除 / 图章 | [`Layer`](#layer)：`ink.line(a,b)` / 不调用就是抬笔 / `ink.clear()` / 单独画一次不存 |
| 造型：下一个造型 | [`c.image(tex, box, srcPx)`](#c-image) 切精灵表的一帧 |
| 播放声音 / 一直播放 | [`audio::play()`](#audio-play) / [`audio::loop()`](#audio-play) |
| 响度 | [`audio::loudness()`](#audio-loudness-spectrum) |
| 音调 | [`Sound::pitch()`](#audio-sound) |
| 变量 / 变量的滑块 | 普通 C++ 变量，绑到 [`ui::slider`](#ui-slider) 上：拖滑块改变量，改变量滑块也跟着动 |
| 重复执行 | [`App::onFrame`](#onframe) 每帧自动调用 |
| 如果碰到鼠标指针 | [`c.hovered()`](#c-hovered) |
| 当角色被点击 | [`App::onClick`](#onclick) |
| 侦测：询问并等待 | [`file::open()`](#file) 弹系统对话框，同步返回路径 |
| 计时器 | [`Stopwatch`](#stopwatch-bench) |
| 随机数 | [`rng().i(lo,hi)`](#rng) |
| 广播 / 收到广播 | 就是普通函数调用，Easel 没有专门的广播机制 |

### 对应 Processing

| Processing | Easel |
|---|---|
| `setup()` / `draw()` | [`App::onStart`](#app-结构) / [`App::onDraw`](#app-结构) |
| `size(w, h)` | [`app.size(w, h)`](#app-结构) |
| `background(c)` | [`app.background(c)`](#app-结构)（不设就用主题的 `bg`） |
| `fill/noFill/stroke/noStroke` | 同名：[`c.fill()`](#样式) 等 |
| `strokeWeight(px)` | [`c.stroke(color, px)`](#样式) 或 [`c.strokeWidth(px)`](#样式) |
| `line/rect/triangle/ellipse/arc/bezier` | 同名：[`c.line()`](#c-line) [`c.rect()`](#c-rect) [`c.triangle()`](#c-triangle-polygon) [`c.ellipse()`](#c-ellipse) [`c.arc()`](#c-arc) [`c.bezier()`](#c-bezier) |
| `beginShape/vertex/endShape` | 同名：[`c.beginShape()`](#c-beginshape) |
| `pushMatrix/translate/rotate/scale/popMatrix` | [`c.push()`](#c-push-pop) / [`c.translate()`](#c-translate) / [`c.rotate()`](#c-rotate) / [`c.scale()`](#c-scale) / [`c.pop()`](#c-push-pop) |
| `PImage / loadImage / image()` | [`Texture` / `loadTexture()`](#loadtexture) / [`c.image()`](#c-image) |
| `textFont / textSize / text()` | [`c.textSize(px)`](#c-text) / [`c.text()`](#c-text) |
| `noise() / noiseSeed() / noiseDetail()` | 同名：[`easel::noise()`](#noise) |
| `map() / lerp() / dist() / constrain()` | [`remap()`](#remap-lerp-clamp) / `lerp()` / `dist()` / [`clamp()`](#remap-lerp-clamp) |
| `random() / randomSeed()` | [`rng().d()`](#rng) / [`seed()`](#rng) |
| `mousePressed() / mouseX, mouseY` | [`App::onClick`](#onclick) / [`c.mouse()`](#坐标) |
| `keyPressed()` | [`App::onKey`](#onkey) |
| `PVector` | [`Vec2`](#vec2) |
| 五个内置颜色模式 | [`Color::hex/rgb/hsv/gray`](#color) |
| `saveFrame()` | [`app.screenshot(path)`](#调试相关) |

### 对应 openFrameworks

| openFrameworks | Easel |
|---|---|
| `ofApp::setup/update/draw` | [`App::onStart`](#app-结构) / [`App::onFrame`](#app-结构) / [`App::onDraw`](#app-结构) |
| `ofSetWindowTitle / ofSetWindowShape` | [`app.title() / app.size()`](#app-结构) |
| `ofBackground` | [`app.background()`](#app-结构) |
| `ofSetColor(...)` 后跟 `ofDrawXxx` | `c.fill()/c.stroke()` 后跟 `c.xxx()`（状态式，同一套模型） |
| `ofPushMatrix/ofTranslate/ofRotateDeg/ofScale/ofPopMatrix` | [`c.push()/translate()/rotate()/scale()/pop()`](#变换)（注意 oF 转角是**角度**，Easel 是**弧度**） |
| `ofPolyline` | [`c.polyline()`](#c-line) 或 [`Layer`](#layer) |
| `ofImage / loadImage / draw()` | [`Texture / loadTexture() / c.image()`](#loadtexture) |
| `ofTrueTypeFont` | [`c.textSize(px)`](#c-text)、字体来源见 [`Theme::fontPath`](#字体来源) |
| `ofNoise` | [`noise()`](#noise) |
| `ofMap / ofLerp / ofDist / ofClamp` | [`remap() / lerp() / dist() / clamp()`](#remap-lerp-clamp) |
| `ofRandom / ofSeedRandom` | [`rng().d() / seed()`](#rng) |
| `ofSoundPlayer` | [`audio::Sound`](#audio-sound) |
| `ofxGui`（`ofxFloatSlider` 等） | [`ui::slider/toggle/button`](#面板控件-ui) |
| `ofVec2f` / `glm::vec2` | [`Vec2`](#vec2) |
| `ofRectangle` | [`Rect`](#rect) |

（第一张表 14 行，第二张表 18 行，第三张表 15 行。）

---

## 1. 结构

一个 Easel 程序由一个 App、几个回调和一个 main 组成。

<a id="app-结构"></a>
### `App`：一个窗口

```cpp
int main(int argc, char** argv) {
    easel::App app(argc, argv);
    app.title("我的作品").size(1280, 800).theme(easel::Theme::Forest());
    app.onDraw([](easel::Canvas& c) { ... });
    app.onPanel([] { ... });
    return app.run();
}
```

`App` 的构造函数会顺手做两件事：解析命令行参数（`cli::args()` 之后就能用）、
装好崩溃处理器。设置项都是链式调用，返回 `App&`，可以连着写。
对应：Processing 隐藏了 `main`、只留 `setup/draw`；Easel 保留显式 `main`。

`statusBar(bool)`：底部状态栏（状态灯 + `app.status()` 的文字 + 缩放倍数）。默认不显示——
交付给评委的时候画面不该有一行开发者才关心的信息；调试时按 F12 会临时出现。

<a id="onframe"></a>
### 回调：每帧都会被重新调用一遍

| 回调 | 什么时候 | 参数 |
|---|---|---|
| `onFrame(fn(double dt))` | 每帧最先调用 | `dt` 秒，帧间隔 |
| `onStart(fn())` | 窗口和显卡都准备好之后，只调一次 | 无 |
| `onDraw(fn(Canvas&))` | 每帧，画布区域 | 画布 |
| `onPanel(fn())` | 每帧，右侧面板区域 | 无（配合 `ui::` 用） |
| `onClick(fn(Vec2, Mouse))` | 点了一下画布 | 世界坐标、哪个键 |
| `onDrag(fn(const Drag&))` | 拖拽中的每一帧 | 见下面 `Drag` |
| `onKey(fn(int))` | 某个键刚按下的那一瞬间 | `ImGuiKey`，如 `ImGuiKey_F` |
| `onWindow(fn(const Rect&))` | 每帧，把画布那块区域整个交给你自己用 ImGui 画 | 那块区域的屏幕矩形 |

`onStart` 只调一次，而且是**唯一**能调 `loadTexture` 的地方——窗口和显卡在
`run()` 之前还不存在，此时调用会失败。`onWindow` 是给不需要画布、只要一堆控件的
工具类程序用的（比如整个界面就是一个表格），可以和 `onDraw` 同时用，`onWindow`
画在上面。

这是一个函数体、没有 class 的立即模式（immediate mode）：`onDraw` 里没有「创建一个
圆形对象」这种东西，每帧从头把想画的东西再画一遍。状态存在全局
`struct State S;` 里——普通函数 + 全局状态，心智模型足够简单，不强制学 OOP。

一个 `App`、几个回调、一个 `main`、一句 `app.run()`——一个 Easel 程序的结构就是这些，
一个文件就能写完（形状见 `template-hello/src/app.cpp`）。

### 算法类作品的惯用结构

作品的核心是一个算法时（路线优化、排序、搜索……），把它单独放在 `src/solver.cpp`，
这一份代码有三个入口：

```
src/solver.cpp   ← 你唯一写算法的文件：数据结构 + solve() + 命令行 main
   │
   ├─ 命令行  g++ -std=c++17 -DEASEL_STANDALONE src/solver.cpp && ./a.out
   ├─ 测试    tests/test_solver.cpp   #include "../src/solver.cpp" + doctest
   └─ 界面    src/app.cpp             #include "solver.cpp" + <easel/easel.h>
```

`solver.cpp` 是一份完整、能自己编译运行的程序：文件末尾一段 `#ifdef EASEL_STANDALONE`
包着 `main`。`app.cpp` 和 `tests/test_solver.cpp` 用 `#include` 把它整个搬过去（unity
include）——这样三个入口是**同一份代码**，断点打在 `solver.cpp` 上，三个入口都能停。
规矩：`solver.cpp` 只 `#include` 标准库和 `easel_core.h`（或 `<easel/core.h>`），不能出现
`<imgui.h>`；每个可执行文件恰好 `#include` 它一次。

```cpp
// solver.cpp 末尾
#ifdef EASEL_STANDALONE
int main(int argc, char** argv) {
    cli::parse(argc, argv);        // 处理 --seed / --case 之类
    installCrashHandler();         // 段错误也留一份调用栈
    ...
}
#endif
```

`App` 的构造函数里已经自动调了 `cli::parse` 和 `installCrashHandler`；只有
`EASEL_STANDALONE`（命令行）那条路径要自己写这两行——`app.cpp` 不用。

<a id="easel-standalone"></a>
### `EASEL_STANDALONE`

编译期开关。定义了它，`solver.cpp` 里 `#ifdef EASEL_STANDALONE` 包住的那段 `main` 会
被编译进去，程序变成一个独立的命令行程序：

```bash
g++ -std=c++17 -DEASEL_STANDALONE src/solver.cpp -o solver && ./solver data/example.json
```

`tests/test_solver.cpp` 和 `app.cpp` include `solver.cpp` 时不定义它，那段 `main` 就
从编译结果里消失，不会和 `doctest` 或 `App` 自己的 `main` 打架。

---

## 2. 画布与坐标

**位置、半径、矩形用世界坐标；线宽、字号用屏幕像素**。滚轮缩放、拖拽平移都
是相机在动，世界坐标本身不变——所以线宽不会跟着放大变粗，这也是「不随缩放变形的点」
要单独有个 `dot()` 的原因。

**世界坐标系**：y 轴向下，和屏幕、Scratch、Processing 一样。往上画就用负数。

<a id="坐标"></a>
### `c.toScreen(w)` / `c.toWorld(s)` / `c.world()` / `c.zoom()` / `c.mouse()`

```cpp
Vec2 s = c.toScreen(worldPoint);   // 世界 -> 屏幕像素
Vec2 w = c.toWorld(screenPoint);   // 屏幕像素 -> 世界
Rect visible = c.world();          // 当前画布能看见的世界范围
double z = c.zoom();               // 一个世界单位 = 多少像素
Vec2 m = c.mouse();                // 鼠标此刻的世界坐标
```

`c.mouse()` **不受变换栈影响**：不管你 `push().translate().rotate()` 了多少层，
鼠标永远是真正的世界坐标，这样点选、拖拽的逻辑不用管画的时候套了几层变换。
对应：Processing 没有这一层（`mouseX/mouseY` 直接是屏幕像素，没有相机概念）；
oF 也一样，通常自己拿 `ofCamera` 才有类似换算。

<a id="c-hovered"></a>
### `c.hovered()`

鼠标是否在画布区域内（不是在右边面板上）。`onClick`/`onDrag` 只在这为真时才触发，
写 `onFrame` 里手动画拖尾（`examples/creative` 的写法）时也要自己判它。
对应：Scratch「碰到鼠标指针？」（这里问的是鼠标碰到整块画布，不是某个角色）。

    if (!c.hovered()) return;
    Vec2 m = c.mouse();

<a id="camera"></a>
### `Camera`：`fit` / `center` / `zoom` / `panZoom` / `scaleBar`

```cpp
void fit(const Rect& worldRect, double paddingPx = 40);  // 把这块区域整个装进画布
void center(const Vec2& w);                              // 这个世界点放到画布中心
void zoom(double pixelsPerUnit);
void panZoom(bool enabled);                               // 关掉滚轮/拖拽（比如工具类程序）
void scaleBar(double* metersPerUnit, const char* unitName = "米");  // 左下角比例尺
```

`app.camera().fit(...)` 是最常用的一行——算完数据之后调一次，之后滚轮缩放、
中键拖拽平移都是白送的。`fit` 在 `run()` 之前调也没关系，会记下来，等第一帧真正
有了视口再执行。`scaleBar` 传的是指针：改了那个 `double` 变量，比例尺立刻跟着变，
不用每帧手动同步。
对应：Processing/oF 都没有相机这层，要自己维护平移缩放矩阵；这里是 Easel 单独给的。

    app.camera().fit(Rect::bounding(points), 60);
    app.camera().scaleBar(&project.unitScale, "米");

**默认交互**：滚轮缩放（以鼠标为中心）、中键或空格+左键拖拽平移。`panZoom(false)`
关掉，工具类程序（只要控件、没有可平移画布）常用得上。

---

## 3. 形状

Canvas 的图元函数。位置是世界坐标，线宽字号是屏幕像素（见上一节）。

<a id="c-line"></a>
### `c.line(a, b)` / `c.polyline(pts, closed=false)`

```cpp
void line(const Vec2& a, const Vec2& b);
void polyline(const std::vector<Vec2>& pts, bool closed = false);
```

一条线段 / 一串点连成的折线，`closed=true` 首尾也连上。
对应：Processing `line()`；`polyline` 对应 oF 的 `ofPolyline`。

    c.stroke(Color::hex(0x4CAF50), 2);
    c.polyline({{0,0},{10,0},{10,10}});

<a id="c-rect"></a>
### `c.rect(r)`

```cpp
void rect(const Rect& r);
```

矩形，`Rect(x, y, w, h)`。填充/描边用不用看当前 `fill`/`stroke` 状态。
对应：Processing `rect(x,y,w,h)`；oF `ofDrawRectangle`。

    c.fill(Color::hex(0x4CAF50));
    c.rect(Rect::fromCenter({0,0}, 20, 10));

<a id="c-circle-dot"></a>
### `c.circle(center, radiusWorld)` / `c.dot(center, radiusPx=5)`

半径单位不一样：`circle` 是世界单位（缩放画布时它跟着变大变小）；`dot` 是屏幕像素
（不管怎么缩放都是那么大的一个点，用来标记位置、不是画真实大小的圆）。
对应：Processing 的 `ellipse(x,y,d,d)`（Processing 没区分「真实大小」和「屏幕大小」的圆，
两者都用 `ellipse`）；`dot` 更接近 Scratch 里角色本身的大小感觉——不随场景缩放。

    c.fill(Color::hex(0xE53935)).circle({0,0}, 3.0);   // 半径 3 个世界单位的圆
    c.fill(Color::gray(0.9f)).dot({0,0}, 6);            // 屏幕上固定 6 像素的点

<a id="c-ellipse"></a>
### `c.ellipse(center, rxWorld, ryWorld)`

椭圆，两个半轴都是世界单位。对应：Processing `ellipse(x,y,w,h)`（注意 Processing 传的
是**直径**，这里传的是**半轴**，数值上差一倍）；oF `ofDrawEllipse`。

    c.noStroke().fill(Color::hex(0x2E7D32));
    c.ellipse({0,0}, 20, 12);

<a id="c-arc"></a>
### `c.arc(center, radius, a0, a1, pie=false)`

圆弧，角度是弧度。`pie=true` 连回圆心画成扇形，否则只画一段弧线（不填充）。
对应：Processing `arc()`（Processing 的 `PIE`/`OPEN`/`CHORD` 模式里，`pie=true` 对应
`PIE`）；oF `ofDrawArc` / 扇形要自己拼三角形，这里内置了。

    c.fill(Color::hex(0xF57C00));
    c.arc({0,0}, 15, 0, 3.14159 / 2, true);   // 一个直角扇形

<a id="c-triangle-polygon"></a>
### `c.triangle(a,b,c)` / `c.polygon(pts)`

```cpp
void triangle(const Vec2& a, const Vec2& b, const Vec2& c);
void polygon(const std::vector<Vec2>& pts);   // 凹的也画得对
```

对应：Processing 同名 `triangle()`；`polygon` 类似 Processing 里用
`beginShape/vertex/endShape` 拼出来的效果，但一行写完，凹多边形也能正确填充。

    c.fill(Color::hex(0x1565C0));
    c.polygon({{0,-10},{8,6},{-8,6}});

<a id="c-bezier"></a>
### `c.bezier(p0, p1, p2, p3)`

三次贝塞尔曲线，`p0`/`p3` 是端点，`p1`/`p2` 是控制点。
对应：Processing 同名 `bezier()`；oF `ofDrawBezier` 或 `ofPath` 的 `bezierTo`。

    c.noFill().stroke(Color::gray(1.f, 0.6f), 1.5);
    c.bezier({0,0}, {10,-12}, {18,12}, {24,0});

<a id="c-beginshape"></a>
### `c.beginShape()` / `c.vertex(p)` / `c.endShape(closed=true)`

自定义形状：先攒点，最后一次画出来。`closed=true` 填充成多边形；`closed=false`
只画折线（不填充）。

对应：Processing 同名 `beginShape/vertex/endShape`（这里没有 Processing 的
`bezierVertex`/`curveVertex` 那些变体，只有直线连接的顶点）。

    c.beginShape();
    for (const Vec2& p : petalShape()) c.vertex(p);
    c.endShape(true);

---

## 4. 颜色与样式

<a id="color"></a>
### `Color`：`hex` / `rgb` / `hsv` / `gray` / `lighter` / `darker` / `mix`

```cpp
Color::hex(0x2E7D32, alpha=1.f)          // 和 CSS/设计稿里的写法一致
Color::rgb(46, 125, 50, A=255)           // 分量 0..255
Color::hsv(120, 0.6, 0.8, alpha=1.f)     // 色相 0..360 度，饱和度/明度 0..1
Color::gray(0.5f, alpha=1.f)
c.withAlpha(0.4f)   c.lighter(0.2f)   c.darker(0.2f)   c.mix(other, 0.5f)
```

内部分量是 `float`，范围 0..1（不是 0..255）——`rgb()`/`hex()` 帮你把常见写法换算过去。
对应：Processing 的 `color()` 默认是 0..255，这里默认 0..1，用 `Color::rgb()` 找回熟悉的手感；
oF 的 `ofColor` 也是 0..255，同理。

    Color base = Color::hex(0x4CAF50);
    Color hi = base.lighter(0.3f).withAlpha(0.8f);

<a id="样式"></a>
### `fill` / `noFill` / `stroke` / `noStroke` / `strokeWidth` / `alpha` / `dashed` / `push` / `pop`

```cpp
Canvas& fill(const Color& c);
Canvas& noFill();
Canvas& stroke(const Color& c, double widthPx);
Canvas& stroke(const Color& c);      // 只改颜色，线宽不变
Canvas& noStroke();
Canvas& strokeWidth(double px);      // 只改线宽
Canvas& alpha(double a);             // 0..1，乘在后面所有颜色上
Canvas& dashed(double onPx, double offPx);
Canvas& solid();                     // 取消虚线
Canvas& push();                      // 存一份样式 + 变换矩阵
Canvas& pop();                       // 还原
```

状态式：设一次，之后画的图元都用该样式，直到下次再设。链式调用可以连着写。
`push`/`pop` 一起存样式和变换矩阵，一起还原——想「画完这个局部就恢复原状」就套一层。
对应：Processing 的 `fill/noFill/stroke/noStroke/strokeWeight/pushStyle/popStyle` 完全同名
同义；`alpha()`/`dashed()` 是 Processing 没有单独函数的（Processing 靠颜色自带的 alpha
分量、`dashed` 则要手写）。

    c.push();
    c.fill(Color::hex(0x43A047)).noStroke();
    c.circle({0,0}, 5);
    c.pop();   // 还原成 push() 之前的样式和变换

<a id="theme"></a>
### `Theme`：五个预设 + 语义色板

```cpp
Theme::Forest()  Theme::Ocean()  Theme::Ember()  Theme::Paper()  Theme::Slate()
Theme::presets()   // 上面五个装进一个 vector，配合下拉框切换用
```

字段：`accent`/`accent2`（主色/次色）、`bg`/`surface`/`fg`/`muted`（画布背景/面板/正文/次要文字）、
`good`/`warn`/`bad`（语义色：成功/警告/危险）、`radius`（圆角）、`fontSize`、`fontPath`。
`Forest`/`Ocean`/`Ember` 是深色，`Paper`/`Slate` 是浅色。主题就是数据，一行切换——
同一批程序各选一个预设，界面不会撞脸。

    app.theme(Theme::Ocean());
    c.fill(app.theme().accent2);   // 用主题的次色，而不是写死一个十六进制数

---

## 5. 变换

变换栈影响的是「接下来画出去的点」，不影响 `c.mouse()`/`c.toWorld()`——那两个永远给
真正的世界坐标。忘记配对 `push()`/`pop()`，变换会一直累积到下一次画东西上。

<a id="c-translate"></a>
### `c.translate(d)`

平移，世界单位。对应：Processing `translate(x, y)`；oF `ofTranslate`；
Scratch 没有对应积木（Scratch 直接改角色的 x/y）。

    c.push().translate({20, 0});
    c.circle({0,0}, 5);
    c.pop();

<a id="c-rotate"></a>
### `c.rotate(angle)`

绕当前原点转，弧度；正角度在屏幕上是顺时针（y 轴向下，和 Processing 一样）。之后画的东西都跟着转；配合 push/pop 用。
对应：Processing `rotate()` · Scratch「右转 15 度」（Scratch 转的是角色，这里转的是接下来的画笔）· oF `ofRotateDeg`（注意 oF 是角度）

    c.push().translate(p).rotate(t);  c.rect(Rect(-2,-1,4,2));  c.pop();

<a id="c-scale"></a>
### `c.scale(s)` / `c.scale(sx, sy)`

等比缩放，或者分轴缩放——`sx` 为负就是水平翻转。
对应：Processing `scale()`；oF `ofScale`。

    c.push().scale(0.7 + 0.3 * std::sin(t));
    c.rect(Rect(-5,-5,10,10));
    c.pop();

<a id="c-push-pop"></a>
### `c.push()` / `c.pop()` / `c.resetMatrix()` / `c.transform(p)`

```cpp
Canvas& push();          // 存一份样式 + 矩阵
Canvas& pop();           // 还原
Canvas& resetMatrix();   // 矩阵清成单位阵（样式不受影响）
Vec2    transform(const Vec2& p) const;   // p 经过当前变换后落在哪（还是世界坐标）
```

后调用的变换先作用在点上：`translate(p).rotate(a)` 表示「先转，再挪过去」——
和 Processing 的 `pushMatrix/translate/rotate` 顺序习惯一致。

    c.push().translate({0,0}).rotate(0.5).translate({10,0});
    Vec2 tip = c.transform({0,0});   // 这个变换会把 (0,0) 挪到哪
    c.pop();

---

## 6. 图片

<a id="loadtexture"></a>
### `loadTexture(path, pixelated=false)`

```cpp
Texture loadTexture(const std::string& path, bool pixelated = false);
void    freeTexture(Texture& t);
```

加载 png/jpg/bmp，失败返回一个空 `Texture`（`if (!tex)` 判断得出来）。
**只能在 `onStart` 里调**——`run()` 之前显卡上下文还没建好。
`pixelated=true`：像素风素材（精灵表、马赛克画风）放大时保持硬边不糊；
默认 `false`：照片、地图这类连续色调的图，线性插值放大更平滑。
对应：Processing `loadImage()`（Processing 没有这个像素/平滑开关，默认插值，
要在 `PImage` 上手动设 `noSmooth()`）；oF `ofImage::load` + `setMinMagFilter(GL_NEAREST)`；
Scratch 造型本身自带矢量/位图缩放，没有这层概念。

    Texture sprite;
    app.onStart([&] { sprite = loadTexture("assets/sprite.png", true); });

<a id="c-image"></a>
### `c.image(tex, worldRect)` / `c.image(tex, worldRect, srcPx)`

```cpp
void image(const Texture& t, const Rect& worldRect);
void image(const Texture& t, const Rect& worldRect, const Rect& srcPx);
```

第一个重载画整张图，铺满 `worldRect`。第二个重载只画图片的一小块——`srcPx` 是
源图上的像素矩形，这就是精灵表（sprite sheet）逐帧动画的写法：换 `srcPx` 就是换一帧。
对应：Processing `image(img, x, y, w, h)` 一整张、`image(img, x,y,w,h, sx,sy,sw,sh)`
子图两个重载；Scratch 的「造型」在这里就是切换 `srcPx`。

    // 4 帧、每帧 32x32 的精灵表，横向排列
    int frame = (int)std::fmod(t * 6.0, 4.0);
    c.image(sheet, Rect::fromCenter({0,0}, 32, 32), Rect(frame * 32.0, 0, 32, 32));

---

## 7. 文字

<a id="c-text"></a>
### `c.text(at, s, align=Align::Left)` / `c.textSize(px)`

```cpp
enum class Align { Left, Center, Right };
Canvas& textSize(double px);   // 字号，像素，状态式
void    text(const Vec2& at, const std::string& s, Align align = Align::Left);
```

`at` 是世界坐标，字号是屏幕像素——字不会随缩放变大变小（这和 Processing 里文字
跟着 `scale()` 一起变大不一样，Easel 的字号永远按屏幕像素算，读起来更稳定）。
对应：Processing `textSize()`/`text()`/`textAlign()`；oF `ofDrawBitmapString` 或
`ofTrueTypeFont::drawString`。

    c.fill(app.theme().fg).textSize(16);
    c.text({0, -20}, "得分：" + std::to_string(score), Align::Center);

<a id="字体来源"></a>
### 中文字体从哪来

不用自己配。Easel 按这个顺序找：`Theme::fontPath`（代码里指定）→ 环境变量
`EASEL_FONT` → `assets/fonts/` 下的内置子集（可选）→ 系统字体（Mac 是苹方/冬青黑 GB，
Windows 是微软雅黑/黑体，Linux 是 Noto Sans CJK）→ 都没有就用 ImGui 内置字体
（中文会变方框，状态灯会变黄，`--doctor` 会说明原因）。Mac/Windows 第 4 步基本不会
落空，所以大多数时候什么都不用做。

    app.theme().fontPath;                      // 留空 = 自动找系统字体
    Theme t = Theme::Forest();
    t.fontPath = "assets/fonts/我的字体.ttf";   // 想固定成某个字体（比如要交视频录像）再设

---

## 8. 画笔（`Layer`）

<a id="layer"></a>
Scratch 的画笔。画一次就留在那儿的笔迹——拖尾、涂鸦、图章、走过的路。

Easel 每一帧都会把整张画布重画一遍（这是相机缩放和回放能成立的前提），所以
「这一笔留下来」这件事没法只靠画一次搞定——`Layer` 就是替你把画过的东西记下来，
每帧由 `Canvas::draw()` 重放一遍的那个本子。它记的是世界坐标，所以缩放平移时
笔迹跟着画面一起动（这和 Scratch 的画笔贴在舞台像素上不同，但对大多数效果更自然）。

```cpp
class Layer {
    Layer& fill(const Color&);  Layer& stroke(const Color&, double px);  ...   // 和 Canvas 同名同义
    void line/polyline/circle/ellipse/dot/rect/triangle/polygon/text/image(...); // 和 Canvas 同名
    void   clear();          // 全部擦除
    size_t size() const;     // 现在记了多少条
    void   limit(size_t maxCommands);   // 改上限，默认 20 万条，超了自动丢最早的
};
```

对应：Scratch 的「落笔」「抬笔」「图章」「全部擦除」；Processing/oF 没有内置的等价物，
通常要自己攒一个点数组。

    struct S { Layer ink; } S;                     // App 的成员变量，不要每帧新建
    S.ink.limit(3000);                             // 尾巴自动变短，超了丢最早的
    void onFrame(double) { if (c.hovered()) S.ink.stroke(Color::hex(0xFFC107), 2).line(prev, c.mouse()); }
    void onDraw(Canvas& c) { c.draw(S.ink); }       // 每帧重放一遍
    // 想「全部擦除」就 S.ink.clear();

---

## 9. 交互

<a id="onclick"></a>
### `App::onClick(fn(Vec2, Mouse))`

```cpp
enum class Mouse { Left, Right, Middle };
```

点了一下画布（鼠标在面板上时不会触发）。参数是世界坐标和按下的是哪个键。
对应：Processing `mousePressed()`；Scratch「当角色被点击」（这里是「当画布被点击」，
自己再判断点到了哪个元素——参考 `template/src/app.cpp` 里「点一下选中最近的点」那段）。

    app.onClick([&](Vec2 w, Mouse b) {
        if (b == Mouse::Left) selected = nearest(w);
    });

<a id="ondrag"></a>
### `App::onDrag(fn(const Drag&))`

```cpp
struct Drag {
    Vec2  start, current, delta;   // 都是世界坐标
    Mouse button = Mouse::Left;
    bool  began = false, ended = false;
};
```

拖拽过程中每一帧都调用一次。`delta` 是「这一帧」移动了多少（不是从头到现在的总量），
`began`/`ended` 标记这一帧是不是拖拽的第一帧/最后一帧。
对应：Processing 没有专门的拖拽回调，通常在 `mouseDragged()` 里自己算 `delta`；
这里是 Easel 帮你算好了。

    app.onDrag([&](const Drag& d) {
        if (d.button == Mouse::Left && selected >= 0) nodes[selected].pos += d.delta;
    });

<a id="onkey"></a>
### `App::onKey(fn(int))`

参数是 `ImGuiKey`（如 `ImGuiKey_F`、`ImGuiKey_Space`），在键**刚按下**的那一帧触发一次
（不是持续触发）。键名去 `vendor/imgui/imgui.h` 搜 `ImGuiKey_`，规律是字母/数字/常见键
直接对应（`ImGuiKey_A`、`ImGuiKey_0`、`ImGuiKey_LeftArrow`、`ImGuiKey_Enter`……）。
对应：Processing `keyPressed()` + `key`/`keyCode`；Scratch「当按下某按键」。

    app.onKey([](int key) {
        if (key == ImGuiKey_R) resetScene();
    });

「按住持续生效」（不是刚按下那一下）要在 `onFrame` 里自己判：

    app.onFrame([](double) { if (ImGui::IsKeyDown(ImGuiKey_Space)) ...; });

---

## 10. 面板控件（`ui::`）

<a id="面板控件-ui"></a>
一层薄词汇，每个函数十几行，包的就是 ImGui。**Easel 没有的控件，直接写
`ImGui::XXX`，两者可以混在同一个 `onPanel` 里**——`ui::` 是词汇不是墙，不强制只用
这些。`#include <easel/easel.h>` 已经带上了 `<imgui.h>`/`<implot.h>`，F12 → 帮助
里能打开 ImGui 和 ImPlot 的官方示例窗口，那是活的参考手册。

<a id="ui-slider"></a>
```cpp
bool section(const char* label, bool defaultOpen = true);   // 折叠分组，返回值决定要不要画里面的控件
bool slider(const char* label, double* v, double lo, double hi, const char* fmt = "%.2f");
bool slider(const char* label, float*  v, float  lo, float  hi, const char* fmt = "%.2f");
bool slider(const char* label, int*    v, int    lo, int    hi);
bool toggle(const char* label, bool* v);
bool button(const char* label, bool wide = false);
```

**双向绑定**的意思：`v` 是唯一真相源。拖滑块会改 `*v`；反过来代码里直接改了
`*v`，下一帧滑块的位置也跟着变——没有「同步」这个动作，因为读的和画的是同一个变量。
返回值是 `true` 表示这一帧发生了变化（用来触发「改了参数就重算」）。
对应：Processing 自己没有内置控件面板，通常配 G4P/ControlP5 库；`slider`/`toggle`/`button`
对应它们里同名的控件；oF 对应 `ofxGui` 的 `ofxFloatSlider`/`ofxToggle`/`ofxButton`；
Scratch 的变量滑块就是最直接的类比。

    if (ui::section("参数")) {
        if (ui::slider("初始温度", &T0, 1.0, 5000.0)) resolve();
        ui::toggle("显示标签", &showLabels);
    }

<a id="ui-stat-chart"></a>
```cpp
void stat(const char* label, const std::string& value, const char* unit = "");
void stat(const char* label, double value, const char* unit = "", int decimals = 2);
void stat(const char* label, int value, const char* unit = "");
void chart(const char* label, const std::vector<double>& ys, const char* seriesName = "值", float height = 140.f);
void chart(const char* label, const std::vector<double>& xs, const std::vector<double>& ys, ...);
```

`stat` 是一个大数字 + 单位 + 说明的卡片，面板上最像「软件」的元素。`chart` 是折线图
（底层是 ImPlot），收敛曲线、`EASEL_TRACE` 记录下来的序列都用它画。
对应：Processing 没有内置的图表控件；这块更接近仪表盘/科学计算软件的东西。

    ui::stat("比较次数", (int)f.cmp);
    std::vector<double> ys; for (auto& x : tl.frames()) ys.push_back(x.cost);
    ui::chart("##conv", ys, "目标值");

```cpp
void separator(); void spacing(); void sameLine();
void title(const char* text);   // 一行小标题
void help(const char* text);    // 灰色说明文字，自动换行
```

排版小工具，懒得写 `ImGui::` 前缀的时候用；本质就是套了一层的 `ImGui::Separator()` 等。

---

## 11. 数学与随机

<a id="vec2"></a>
### `Vec2`

```cpp
struct Vec2 { double x, y; };
a + b   a - b   a * s   a / s   -a          // 运算符
a.length()   a.length2()   a.normalized()   a.perp()   a.angle()   a.rotated(rad)
```

`length2()` 是长度的平方（省一次开方，只比较大小时用它更快）。`perp()` 垂直向左（标准数学坐标系中逆时针 90°；世界坐标 y 向下时视觉上也是左转）。对应：Processing `PVector`；oF `ofVec2f`/`glm::vec2`；Scratch 没有向量类型，
角色的 x/y 就是散着用的两个数。

<a id="rect"></a>
### `Rect`

```cpp
struct Rect { double x, y, w, h; };          // x/y 是左上角
Rect::fromCorners(a, b)   Rect::fromCenter(c, w, h)   Rect::bounding(点数组)
r.left()/right()/top()/bottom()   r.min()/max()   r.center()   r.empty()
r.contains(p)   r.overlaps(other)   r.expanded(px)   r.united(other)
```

`bounding()` 常用来给 `camera().fit()` 算范围。对应：oF `ofRectangle`（同名方法基本
对得上：`getCenter`→`center()`、`inside`→`contains()`）；Processing 没有矩形类型，
直接传四个数。

    Rect box = Rect::bounding(points).expanded(10);
    app.camera().fit(box);

<a id="remap-lerp-clamp"></a>
### `dist` / `dot` / `cross` / `lerp` / `clamp` / `remap`

```cpp
double dist(a, b)     double dist2(a, b)     double dot(a, b)     double cross(a, b)
Vec2   lerp(a, b, t)  double lerp(a, b, t)   double clamp(v, lo, hi)
double remap(v, lo, hi, lo2, hi2)
```

把 `v` 从 `[lo,hi]` 这段范围搬到 `[lo2,hi2]` 上，不夹紧（超出范围结果也超出，
要夹就再套一层 `clamp`）。最常见的用法是把「数据」变成「画面」：下标→位置、
温度→颜色、噪声值→半径。

对应 Processing 的 `map()`。

    double r = remap(t, 0, 1, 5, 50);      // t 从 0 涨到 1，半径跟着从 5 涨到 50

<a id="rng"></a>
### `rng()` / `Rng` / `seed()`

```cpp
class Rng {
    int    i(int lo, int hi);        // 闭区间随机整数
    double d(double lo=0, double hi=1);
    bool   chance(double p);
    double normal(double mean=0, double sd=1);
    template<class T> void shuffle(std::vector<T>& v);
    template<class T> const T& pick(const std::vector<T>& v);
};
Rng& rng();          // 全局随机源
void seed(unsigned); unsigned current_seed();
```

**默认每次运行都不一样，但种子永远看得见、永远能复现**：不加 `--seed` 时，`rng()`
启动就换一颗新种子；启动时 `cli::parse` 会打一行日志告诉你这次用的是哪颗
（`本次随机种子 N（想复现这一次：--seed N）`），想再走一次同样的路，把这个 N
抄进 `--seed N` 就行——「界面里看到不对 → 导出用例 → 命令行复现」依赖的正是这个。
`--doctor` 里也能看到当前种子。
算法类工程要「同一条命令永远给同一个结果」（调试算法时改一行代码好对比），
在 `main` 里 `cli::parse` 之后显式 `seed(EASEL_FIXED_SEED)` 钉死就行——`template/`
里的两个入口已经这样写了；做视觉/创意作品的话删掉那一行，让它保持随机。
`noise()` 默认跟着同一颗种子走（`seed(x)` 会连带把 `noise` 换到第 x 片山）；
只想单独换画面、不动 `rng()`，用 `noiseSeed(x)`。
对应：Processing `random()`/`randomSeed()`；oF `ofRandom()`/`ofSeedRandom()`；
Scratch 的「在 x 到 y 间随机取一个数」对应 `rng().i()`/`rng().d()`。

    rng().shuffle(deck);
    int hp = rng().i(1, 100);
    if (rng().chance(0.3)) spawn();

<a id="noise"></a>
### `noise` / `noiseSeed` / `noiseDetail`

```cpp
double noise(double x);              double noise(double x, double y);
double noise(double x, double y, double z);
void   noiseSeed(unsigned s);
void   noiseDetail(int octaves, double falloff = 0.5);   // 层数 1~8，每层音量 0~1
```

和 `rng()` 不是一回事：`rng()` 相邻调用之间毫无关系（雪花点），`noise()` 相邻的输入
给相邻的输出——画出来是山脉、云、飘动的草，而不是噪点。返回值永远在 `[0,1]`。
一维当时间用，二维当地形/云，三维常用来做「会动的二维图案」（第三维喂时间）。
对应：Processing 同名 `noise()`/`noiseSeed()`/`noiseDetail()`；oF `ofNoise()`。

    for (int i = 0; i < 200; ++i)
        c.dot({i * 0.5, remap(noise(i * 0.05, t), 0, 1, -5, 5)});   // 一条起伏的地形线

<a id="stopwatch-bench"></a>
### `Stopwatch` / `bench::run`

```cpp
class Stopwatch { void reset(); double ms() const; double s() const; };
template<class F> bench::Result bench::run(F&& f, int repeats = 5);
```

`Stopwatch` 就是掐表；`bench::run` 跑几次取最快/平均/总计，返回一个能直接
`.str()` 打印的结果。

    Stopwatch sw;
    solve();
    EASEL_LOG("用时 %.2f ms", sw.ms());

    auto r = bench::run([]{ solve(); }, 5);
    std::cout << r.str();   // "5 次：最快 12.3 ms，平均 13.1 ms，总计 65.4 ms"

---

## 12. 声音

<a id="audio-play"></a>
### `audio::play` / `audio::loop` / `audio::stop` / `audio::volume`

```cpp
bool play(const std::string& path);   // 放一遍，wav/mp3/flac 都行，内置解码器
bool loop(const std::string& path);   // 循环播（背景音乐）
void stop();                          // 全停
void volume(double v);   double volume();   // 总音量 0..1
```

路径不对、或者这台机器根本没声卡，都只返回 `false`——程序不会因为没声音就崩。
对应：Scratch「播放声音」「重复播放」「音量」积木。

    audio::loop("assets/bgm.mp3");
    app.onClick([](Vec2, Mouse) { audio::play("assets/click.wav"); });

<a id="audio-loudness-spectrum"></a>
### `audio::loudness()` / `audio::spectrum(bands)`

```cpp
double loudness();                          // 此刻放出去的响度 0..1
std::vector<float> spectrum(int bands=64);  // 低频到高频 bands 个格子，每格 0..1
```

`loudness()` 是**放出去的**响度，不是麦克风——麦克风要开采集设备、要权限弹窗，Easel
不做这个。`spectrum` 的值做过时间平滑，画柱状图不会一帧一个样。
对应：Scratch「响度」积木（这里是喇叭的响度，不是麦克风的响度，用之前留意一下）。

    std::vector<float> bands = audio::spectrum(32);
    for (size_t i = 0; i < bands.size(); ++i)
        c.rect(Rect(i * 2.0, 20 - bands[i] * 20, 1.6, bands[i] * 20));

<a id="audio-sound"></a>
### `audio::Sound` / `audio::load`

```cpp
class Sound {
    static Sound load(const std::string& path);
    void play();  void stop();  void volume(double v);  void pitch(double p);  // 1.0=原速原调
    bool loop = false;                         // 下一次 play() 生效
    explicit operator bool() const;            // 加载失败/没声卡 -> false，安全空操作
};
```

短音效反复用：`load` 一次，`play` 很多次。拷贝它得到的是同一个声音（内部共享），
可以放进 `vector` 里。同一时刻只响一次——还在响的时候再 `play()`，从头重放
（和 Scratch 一样）。
对应：oF `ofSoundPlayer`；Scratch 里每个「播放音效」积木背后就是一个 `Sound`。

    audio::Sound beep = audio::load("assets/beep.wav");
    beep.volume(0.3); beep.pitch(1.5);
    beep.play();

**没声卡时的行为**：`audio::ok()` 返回 `false`，所有 `audio::` 调用静默失败（不崩、
不报错刷屏），`audio::doctor()`/F12 自检页会说明原因。这种情况并不少见，
写作品时不要假设 `audio::play` 一定「响了」。

---

## 13. 回放

<a id="timeline"></a>
### `Timeline<T>` / `App::transport`

```cpp
template<class T> class Timeline {
    void load(std::vector<T> frames);   void push(const T&);   void clear();
    const T& current() const;   const T& at(int i) const;
    std::vector<T>& frames();   size_t size() const;
};
// 播放控制（TimelineBase，与 T 无关）：
play() pause() toggle() step(±1) seek(i) speed(s) fps(f) rewind() loop
```

`App::transport(tl)` 把它接到窗口底部的播放条上（拖进度条、倍速、单步都是白送的）。
对应：Processing/oF 都没有内置的「历史回放」概念，通常要自己写状态机。

**「算完 + 回放」，不是边算边画**：`solve()` 是一个纯函数——喂参数进去，一次性把
全过程采样成一串 `Frame` 返回；界面退化成一个播放器，白拿进度拖拽、倍速、单步、
重播，`solve()` 本身也好写好测（doctest 直接调用它，不用管界面）。代价是暴力算法
如果本身很慢会卡住界面一下——题目规模要控制在几百毫秒内能算完，更大的规模
只在文档里报告离线跑的结果。

    Timeline<Frame> tl;
    tl.load(solve(project, params));   // 一次算完，返回全过程
    app.transport(tl);
    // onDraw 里：const Frame& f = tl.current();

---

## 14. 数据与文件

<a id="easel-json"></a>
### `EASEL_JSON(Type, 字段...)`

```cpp
struct Node { std::string name; Vec2 pos; int trash = 0; };
EASEL_JSON(Node, name, pos, trash)     // 写在结构体外面

class Foo { EASEL_JSON_MEMBER(Foo, a, b); private: int a, b; };  // 私有成员用这个，写在结构体里面
```

给结构体加上 JSON 读写能力——存档、导出调试用例、F12 状态页都靠它。加了之后
`json j = node; Node n = j.get<Node>();` 就能用。对应：Processing/oF 都没有内置的
结构体序列化，通常手写或者引入额外的库。

<a id="fs"></a>
<a id="file"></a>
### `fs::` / `file::`

```cpp
bool   fs::exists(path);       bool fs::makeDirs(path);
std::string fs::readText(path, bool* ok=nullptr);   bool fs::writeText(path, content);
json   fs::loadJson(path);     bool fs::saveJson(path, j, indent=2);
std::string file::open(filterName="工程文件", extensions="json");   // 系统对话框，取消返回空串
std::string file::save(defaultName="project.json", filterName=..., extensions=...);
std::string file::folder(defaultPath=nullptr);
```

`fs::loadJson` 读不到文件或格式错都返回 `json()`（null）并打一条 warn，**不抛异常**，
不用写 `try/catch`。`file::open`/`file::save`/`file::folder` 弹的是系统原生对话框，
用户取消就是空字符串，记得判断。
对应：Processing `loadJSONObject/saveJSONObject`、`selectInput/selectOutput`；
oF `ofLoadJson`（或手动读文件）、`ofSystemLoadDialog`。

    std::string p = file::open("工程文件", "json");
    if (!p.empty()) project = fs::loadJson(p).get<Project>();

<a id="cli-args"></a>
### `cli::args()`

```cpp
cli::args().has("solve")            // 有没有这个 flag
cli::args().str("case", "")         // 取字符串参数，带默认值
cli::args().num("iters", 200)       // 取整数参数
cli::args().real("alpha", 0.5)      // 取浮点参数
cli::args().at(0, "data/example.json")   // 第 i 个位置参数（不带 -- 的那些）
```

自己的命令行参数（不是 Easel 内置的那些）用这一套取。`App` 构造时已经调过
`cli::parse`，`EASEL_STANDALONE` 的 `main` 里要自己调一次。

    int iters = (int)cli::args().num("iters", 200);
    std::string casePath = cli::args().str("case");

---

## 15. 调试

<a id="调试宏"></a>
### `EASEL_LOG` / `EASEL_WARN` / `EASEL_ERROR` / `EASEL_TRACE` / `EASEL_CHECK` / `EASEL_CHECK_EQ` / `EASEL_CHECK_NEAR`

**同一行代码，命令行和界面里表现不一样**：

| 宏 | 命令行里 | 界面里 |
|---|---|---|
| `EASEL_LOG("第 %d 轮", i)` | 打到终端（printf 风格） | 日志窗（带 file:line、帧号） |
| `EASEL_WARN(...)` / `EASEL_ERROR(...)` | 打到 stderr | 日志窗，颜色区分级别 |
| `EASEL_TRACE("长度", cur.len)` | 打一行 CSV（`TRACE,名字,序号,值`） | 自动画成折线图 |
| `EASEL_CHECK(cond, "说明")` | 条件为假就打印调用栈然后 abort | 条件为假弹红色横幅，暂停回放，**不崩** |
| `EASEL_CHECK_EQ(a, b)` | 同上，外加把两边的值都打出来 | 同上 |
| `EASEL_CHECK_NEAR(a, b, eps)` | 同上，允许 `eps` 误差 | 同上 |

`cout`/`printf` 也可以随便写：命令行在终端看，界面里会同时进日志窗（顺带解决了
Windows GUI 子系统吞输出的老问题）。

    EASEL_TRACE("目标值", best);
    EASEL_CHECK(history.back().order.size() == p.nodes.size(), "解的长度和点数对不上");

<a id="dbg"></a>
### `easel::dbg(key, value)`

在 F12 调试台的「状态」页显示一行，实时刷新。命令行下什么都不做（不刷屏）。
`value` 可以是任何能 `<<` 到 `ostream`、或者能转成 `json` 的类型。

    dbg("当前解", history.back().cost);
    dbg("已选中", U.selected);

<a id="调试相关"></a>
### F12 调试台六页

| 页 | 回答的问题 |
|---|---|
| 日志 | 我打的东西去哪了（级别过滤、重复折叠、一键复制） |
| 追踪 | 这个数是怎么变的（`EASEL_TRACE` 自动成图） |
| 状态 | 现在到底是什么（`dbg()` 实时表格） |
| 画布 | 为什么什么都没画出来（图元数、视口外个数、看不见的个数、世界范围） |
| 用例 | 怎么把问题带回单文件（导出 + 复现命令 + `debug/` 目录列表） |
| 自检 | 环境有没有问题（后端、显卡、DPI、字体、种子、工作目录、编译器、帧率） |

状态栏最左边那个点：绿=正常，黄=有警告（比如没找到中文字体），红=断言失败或崩过。
按 F12 开调试台——状态栏默认不显示，F12 打开时会跟着一起出现。

### 导出调试用例 → 命令行复现

```cpp
App& onExportCase(std::function<json()> fn);            // 告诉「导出」按钮要导出什么
std::string exportDebugCase(const json& state, const std::string& note = {});
```

不接 `onExportCase`，导出按钮是灰的。F12 → 用例 → 点导出，会写一个
`debug/case-003.json`（包含当时的种子、命令行、state），并打印一行可以直接粘到终端
的复现命令：`./solver --case debug/case-003.json --seed 20260101`。这条闭环——
**界面里看到不对 → 导出用例 → 命令行复现 → 用熟悉的方式定位 → 改 → 跑测试 → 回界面**——
成立的前提是种子固定（见 [`rng()`](#rng)）。

    app.onExportCase([] { return json{{"project", S.project}, {"params", S.params}}; });

命令行侧对应读回：`debug::importCase(path)`，`solver.cpp` 的 `main` 里判断
`cli::args().str("case")` 不为空就调它。

### 对拍器 `EASEL_CROSSCHECK`

```cpp
auto gen  = [](Rng& r) { return input; };       // 造一组随机输入
auto fast = [](const Input& in) { return ans; }; // 你写的快算法
auto slow = [](const Input& in) { return ans; }; // 笨但一定对的
auto rep = EASEL_CROSSCHECK(200, gen, fast, slow);   // 不一致就存 debug/duipai-001.json
```

第 `k` 轮用的种子是 `起始种子 + k`，出错那一轮能单独用 `--seed` 复现。返回值/输入类型
要能用 `==` 比较，或者能转成 `json`。

### `--doctor`

```cpp
std::string doctorCore();     // core.h 部分：编译器、C++ 标准、平台、Debug/Release、ASan、种子、cwd
std::string App::doctor() const;   // 完整版：core + 渲染后端 + 字体 + DPI
```

出问题时第一件事：`./solver --doctor` 或 `app --doctor`，不开窗口，打完就退出。

---

## 16. 命令行参数一览

App 内置的（构造 `App` 时自动解析）：

| 参数 | 作用 |
|---|---|
| `--open <文件>` | 启动时打开它（用 `app.openPath()` 取） |
| `--solve` | 打开之后直接算（用 `app.wantsSolve()` 判断） |
| `--seed N` | 指定随机种子（不给的话每次运行自动换一个新的，启动时会打日志告诉你是哪个） |
| `--case <文件>` | 载入一个调试用例 |
| `--doctor` | 打印环境自检然后退出，不开窗口 |
| `--debug` | 启动就把 F12 调试台打开 |
| `--edit` | 启动就把 F9 编辑栏打开 |
| `--edit-run` | 打开编辑栏并立刻编译运行一次（CI 用） |
| `--export [目录]` | 导出一个能独立编译的完整工程，然后退出 |
| `--quiet` | `EASEL_TRACE`/`EASEL_LOG` 不往终端刷屏 |
| `--frames N` | 跑 N 帧自动退出（CI / 截图用） |
| `--screenshot <png>` | 退出前存一张截图 |

自己的参数用 `cli::args()`（见 [第 14 节](#cli-args)）。

---

## 17. 快捷键一览

| 键 | 作用 |
|---|---|
| **F5** | 编辑栏内：保存并编译运行命令行版 `solver` |
| **F9** | 开/关 F9 编辑栏（默认关着，发布出去时不该有它） |
| **F12** | 开/关调试台（状态栏默认不显示，F12 打开时会一并出现） |
| **Esc** | 关闭欢迎页 |
| 滚轮 | 缩放（以鼠标为中心） |
| 中键拖拽 | 平移画布 |
| 空格 + 左键拖拽 | 平移画布（没有中键鼠标时用） |
| Ctrl + 滚轮 | 编辑栏里调字号 |
| Ctrl + S | 编辑栏里保存 |

---

## 18. 常见错误

**画布一片空白** → F12 → 画布页看图元数是不是 0：`onDraw` 是不是提前 `return` 了；
如果图元数不是 0 但「视口外」数量很大，多半是没调 `camera().fit(...)`，画的东西和
相机看的范围对不上。

**中文变成方框** → 没找到中文字体。F12 → 自检 看字体来源；设
`Theme::fontPath = "assets/fonts/xxx.ttf"`，或者设环境变量 `EASEL_FONT`。

**点了画布没反应** → 鼠标在右边面板上时不会触发 `onClick`/`onDrag`；按住空格
拖拽是平移，不是点击。

**`loadTexture` 返回空 `Texture` / 直接崩了** → 只能在 `App::onStart` 里调用；
`onStart` 之前（比如全局变量初始化时、`run()` 之前）显卡上下文还没建好。

**图片路径读不到（哪怕文件确实在）** → 相对路径是相对**当前工作目录**，不是相对
exe 所在目录、也不是相对源码目录。工作台/编辑栏运行时工作目录通常是工程根目录；
双击 exe 运行时可能不是。`--doctor` 会打印当前工作目录，先用 `fs::exists(path)` 检查。

**贴图放大后一格一格发糊** → 像素风素材（精灵表、马赛克）没传 `pixelated=true`：
`loadTexture("sheet.png", true)`。照片、地图这类连续色调的图不用管，默认插值更平滑。

**改了 `solver.cpp`，`App` 里没变化** → 不是热重载。`solver.cpp` 被三个入口各自
`#include` 一次，改完要重新构建对应的 target（工作台的「编译并运行」/`cmake --build`）。

**没声卡的机器上 `audio::` 什么都不响** → 这是设计好的行为，不是 bug：
`audio::ok()` 会是 `false`，所有 `audio::` 调用静默失败，不会崩。`audio::doctor()`
或 F12 自检页能看到具体原因（比如「找不到输出设备」）。

**数组越界了但程序没报错，直接算出错误结果** → Release 构建默认不做越界检查。
用 `cmake --preset debug`（开 ASan + 标准库调试模式）重新构建，越界会当场停下来，
比 `EASEL_CHECK` 更早发现问题。

**崩溃了但调用栈看不出是哪一行** → 用 VS Code 的 F5（Debug 配置）再跑一次，或者
用 `cmake --preset debug`。`installCrashHandler()` 打印的调用栈在某些平台上只有
函数地址、没有行号，Debug 构建 + 断点定位更直接。

---

Easel · 代码酷 daimaku.net · MIT · 对应 API 冻结前的 v0.1.1（开发中）
