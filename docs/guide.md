# Easel 使用指南

给"人"看的那部分：从熟悉的工具类比过来怎么找、Easel 项目该怎么组织、
以及一些设计决定背后的原因。纯签名/参数/返回值查 [reference.md](reference.md)，
一页纸速查表见 [cheatsheet.md](cheatsheet.md)。

---

## 1. 从熟悉的名字找起

Easel 把 Scratch、Processing、openFrameworks 三种写法接到同一个库上。
三张对照表：左列是 Scratch / Processing / openFrameworks 里的名字，右列是 Easel 里的写法
（链接指向 [reference.md](reference.md) 里的完整签名）。

### 对应 Scratch

| Scratch 积木 | Easel 里的写法 |
|---|---|
| 移动 10 步 / 面向 90 方向 | [`Vec2`](reference.md#vec2) 加减、[`c.rotate()`](reference.md#c-rotate) |
| 画笔：落笔 / 抬笔 / 全部擦除 / 图章 | [`Layer`](reference.md#layer)：`ink.line(a,b)` / 不调用就是抬笔 / `ink.clear()` / 单独画一次不存 |
| 造型：下一个造型 | [`c.image(tex, box, srcPx)`](reference.md#c-image) 切精灵表的一帧 |
| 播放声音 / 一直播放 | [`audio::play()`](reference.md#audio-play) / [`audio::loop()`](reference.md#audio-play) |
| 响度 | [`audio::loudness()`](reference.md#audio-loudness-spectrum) |
| 音调 | [`Sound::pitch()`](reference.md#audio-sound) |
| 变量 / 变量的滑块 | 普通 C++ 变量，绑到 [`ui::slider`](reference.md#ui-slider) 上：拖滑块改变量，改变量滑块也跟着动 |
| 重复执行 | [`App::onFrame`](reference.md#onframe) 每帧自动调用 |
| 如果碰到鼠标指针 | [`c.hovered()`](reference.md#c-hovered) |
| 当角色被点击 | [`App::onClick`](reference.md#onclick) |
| 侦测：询问并等待 | [`file::open()`](reference.md#file) 弹系统对话框，同步返回路径 |
| 计时器 | [`Stopwatch`](reference.md#stopwatch-bench) |
| 随机数 | [`rng().i(lo,hi)`](reference.md#rng) |
| 广播 / 收到广播 | 就是普通函数调用，Easel 没有专门的广播机制 |

### 对应 Processing

| Processing | Easel |
|---|---|
| `setup()` / `draw()` | [`App::onStart`](reference.md#app-结构) / [`App::onDraw`](reference.md#app-结构) |
| `size(w, h)` | [`app.size(w, h)`](reference.md#app-结构) |
| `background(c)` | `app.background(c)`（不设就用主题的 `bg`） |
| `fill/noFill/stroke/noStroke` | 同名：[`c.fill()`](reference.md#样式) 等 |
| `strokeWeight(px)` | [`c.stroke(color, px)`](reference.md#样式) 或 [`c.strokeWidth(px)`](reference.md#样式) |
| `line/rect/triangle/ellipse/arc/bezier` | 同名：[`c.line()`](reference.md#c-line) [`c.rect()`](reference.md#c-rect) [`c.triangle()`](reference.md#c-triangle-polygon) [`c.ellipse()`](reference.md#c-ellipse) [`c.arc()`](reference.md#c-arc) [`c.bezier()`](reference.md#c-bezier) |
| `beginShape/vertex/endShape` | 同名：[`c.beginShape()`](reference.md#c-beginshape) |
| `pushMatrix/translate/rotate/scale/popMatrix` | [`c.push()`](reference.md#c-push-pop) / [`c.translate()`](reference.md#c-translate) / [`c.rotate()`](reference.md#c-rotate) / [`c.scale()`](reference.md#c-scale) / [`c.pop()`](reference.md#c-push-pop) |
| `PImage / loadImage / image()` | [`Texture` / `loadTexture()`](reference.md#loadtexture) / [`c.image()`](reference.md#c-image) |
| `textFont / textSize / text()` | [`c.textSize(px)`](reference.md#c-text) / [`c.text()`](reference.md#c-text) |
| `noise() / noiseSeed() / noiseDetail()` | 同名：[`easel::noise()`](reference.md#noise) |
| `map() / lerp() / dist() / constrain()` | [`remap()`](reference.md#remap-lerp-clamp) / `lerp()` / `dist()` / [`clamp()`](reference.md#remap-lerp-clamp) |
| `random() / randomSeed()` | [`rng().d()`](reference.md#rng) / [`seed()`](reference.md#rng) |
| `mousePressed() / mouseX, mouseY` | [`App::onClick`](reference.md#onclick) / [`c.mouse()`](reference.md#坐标) |
| `keyPressed()` | [`App::onKey`](reference.md#onkey) |
| `PVector` | [`Vec2`](reference.md#vec2) |
| 五个内置颜色模式 | [`Color::hex/rgb/hsv/gray`](reference.md#color) |
| `saveFrame()` | `app.screenshot(path)` |

### 对应 openFrameworks

| openFrameworks | Easel |
|---|---|
| `ofApp::setup/update/draw` | [`App::onStart`](reference.md#app-结构) / [`App::onFrame`](reference.md#app-结构) / [`App::onDraw`](reference.md#app-结构) |
| `ofSetWindowTitle / ofSetWindowShape` | `app.title() / app.size()` |
| `ofBackground` | `app.background()` |
| `ofSetColor(...)` 后跟 `ofDrawXxx` | `c.fill()/c.stroke()` 后跟 `c.xxx()`（状态式，同一套模型） |
| `ofPushMatrix/ofTranslate/ofRotateDeg/ofScale/ofPopMatrix` | [`c.push()/translate()/rotate()/scale()/pop()`](reference.md#变换)（注意 oF 转角是**角度**，Easel 是**弧度**） |
| `ofPolyline` | [`c.polyline()`](reference.md#c-line) 或 [`Layer`](reference.md#layer) |
| `ofImage / loadImage / draw()` | [`Texture / loadTexture() / c.image()`](reference.md#loadtexture) |
| `ofTrueTypeFont` | [`c.textSize(px)`](reference.md#c-text)、字体来源见 [reference.md](reference.md#字体来源) |
| `ofNoise` | [`noise()`](reference.md#noise) |
| `ofMap / ofLerp / ofDist / ofClamp` | [`remap() / lerp() / dist() / clamp()`](reference.md#remap-lerp-clamp) |
| `ofRandom / ofSeedRandom` | [`rng().d() / seed()`](reference.md#rng) |
| `ofSoundPlayer` | [`audio::Sound`](reference.md#audio-sound) |
| `ofxGui`（`ofxFloatSlider` 等） | [`ui::slider/toggle/button`](reference.md#面板控件-ui) |
| `ofVec2f` / `glm::vec2` | [`Vec2`](reference.md#vec2) |
| `ofRectangle` | [`Rect`](reference.md#rect) |

---

## 2. 项目结构与惯例

### 一个 Easel 程序长什么样

一个 Easel 程序由一个 `App`、几个回调和一个 `main` 组成——一个文件就能写完
（形状见 `template-hello/src/app.cpp`）。这是**立即模式**（immediate mode）：
`onDraw` 里没有"创建一个圆形对象"这种东西，每帧从头把想画的东西再画一遍。
状态存在全局 `struct State S;` 里——普通函数 + 全局状态，不强制学 OOP。

对应关系：Processing 隐藏了 `main`，只留 `setup/draw`；Easel 保留显式 `main`。

### 算法类作品的三入口结构

作品核心是一个算法（路线优化、排序、搜索……）时，把它单独放在 `src/solver.cpp`，
这一份代码有三个入口，且是**同一份代码**（断点打在 `solver.cpp` 上，三个入口都能停）：

```
src/solver.cpp   ← 唯一写算法的文件：数据结构 + solve() + 命令行 main
   │
   ├─ 命令行  g++ -std=c++17 -DEASEL_STANDALONE src/solver.cpp && ./a.out
   ├─ 测试    tests/test_solver.cpp   #include "../src/solver.cpp" + doctest
   └─ 界面    src/app.cpp             #include "solver.cpp" + <easel/easel.h>
```

规矩：`solver.cpp` 只 `#include` 标准库和 `easel.hpp`（或 `<easel/core.h>`），
不能出现 `<imgui.h>`；每个可执行文件恰好 `#include` 它一次（unity include）。

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
### `EASEL_STANDALONE` 编译期开关

定义了它，`solver.cpp` 里 `#ifdef EASEL_STANDALONE` 包住的那段 `main` 会
被编译进去，程序变成一个独立的命令行程序：

```bash
g++ -std=c++17 -DEASEL_STANDALONE src/solver.cpp -o solver && ./solver data/example.json
```

`tests/test_solver.cpp` 和 `app.cpp` include `solver.cpp` 时不定义它，那段 `main`
就从编译结果里消失，不会和 `doctest` 或 `App` 自己的 `main` 打架。

---

## 3. 一些设计决定背后的原因

**`frameRate`/`idleThrottle` 为什么存在**：垂直同步在虚拟机/没装驱动的机器上
经常失效，一旦失效又没设帧率上限，主循环会空转烧一个 CPU 核，所以默认给了
60fps 的兜底。`idleThrottle` 是给工作台这类"界面大部分时间都在发呆"的程序用的
——作品一般不用开，画布自己的动画不该被这个打断。`statusBar` 默认不显示，
是因为交付给评委的时候画面不该有一行开发者才关心的信息。

**`Layer` 为什么需要"记笔迹再重放"**：Easel 每一帧都会把整张画布重画一遍
（这是相机缩放和回放能成立的前提），所以"这一笔留下来"这件事没法只靠画一次
搞定——`Layer` 替你把画过的东西记下来，每帧由 `Canvas::draw()` 重放一遍。
它记的是世界坐标，缩放平移时笔迹跟着画面一起动（这和 Scratch 的画笔贴在
舞台像素上不同，但对大多数效果更自然）。

**随机种子为什么默认"每次不同但可复现"**：不加 `--seed` 时 `rng()` 启动就换
一颗新种子，并打一行日志告诉你是哪颗——"界面里看到不对 → 记下种子 →
命令行 `--seed N` 复现"依赖的正是这个。算法类工程要"同一条命令永远给同一个
结果"时，在 `main` 里 `cli::parse` 之后显式 `seed(EASEL_FIXED_SEED)` 钉死；
做视觉/创意作品的话让它保持随机。

**`noise()` 和 `rng()` 为什么是两套东西**：`rng()` 相邻调用之间毫无关系
（雪花点），`noise()` 相邻的输入给相邻的输出——画出来是山脉、云、飘动的草，
而不是噪点。两者用途不同，不能互相替代。

**`Timeline` 为什么是"算完 + 回放"而不是"边算边画"**：`solve()` 是一个纯
函数——喂参数进去，一次性把全过程采样成一串 `Frame` 返回；界面退化成一个
播放器，白拿进度拖拽、倍速、单步、重播，`solve()` 本身也好写好测（doctest
直接调用它，不用管界面）。代价是暴力算法如果本身很慢会卡住界面一下——
题目规模要控制在几百毫秒内能算完，更大的规模只在文档里报告离线跑的结果。

### 调试闭环

整个调试相关的功能（`EASEL_CHECK`、导出用例、`--seed`、`--case`）串起来是一条闭环：

```
界面里看到不对
   → F12 → 用例 → 「导出调试用例」        得到 debug/case-003.json + 一行命令
   → 在终端里跑那行命令                    ./solver --case debug/case-003.json --seed N
   → 用熟悉的方式定位（printf、断点、单步）
   → 改 solver.cpp
   → 跑测试                                确保没改坏别的
   → 回到界面看
```

这条闭环成立的前提是种子固定可复现（见 [reference.md#rng](reference.md#rng)）。

---

Easel · 代码酷 daimaku.net · MIT
