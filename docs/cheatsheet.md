# Easel 速查表

> 完整 API 签名见 [`reference.md`](reference.md)；背后的设计原因、项目结构惯例、
> 与 Scratch/Processing/openFrameworks 的对照见 [`guide.md`](guide.md)。
>
> 两页：第一页是 API，第二页是「出问题了怎么办」。
> 打印出来放在手边。找不到的东西直接调 ImGui。

---

# 第一页 · API

## 算法类作品：三个入口，一份业务代码

作品的核心是一个算法时，单独放一个文件，三个入口共用同一份代码：

```
src/solver.cpp   ← 你唯一写算法的文件
   ├─ 命令行   g++ -std=c++17 -DEASEL_STANDALONE src/solver.cpp && ./a.out
   ├─ 测试     tests/test_solver.cpp   #include "../src/solver.cpp" + doctest
   └─ 界面     src/app.cpp             #include "solver.cpp"        + Easel
```

规矩：`solver.cpp` 只 include `easel.hpp` 和标准库；每个可执行文件恰好 include 它一次；
**断点打在 `solver.cpp` 上，三个入口都能停**。

## 几何

```cpp
Vec2 a{3, 4};                  a.length()  a.normalized()  a.perp()  a.rotated(rad)
dist(a, b)   dot(a, b)   cross(a, b)   lerp(a, b, 0.5)   clamp(v, lo, hi)
Rect r(x, y, w, h);            Rect::bounding(点数组)   Rect::fromCenter(c, w, h)
                               r.center()  r.contains(p)  r.overlaps(q)  r.expanded(10)
Color::hex(0x2E7D32)   Color::rgb(46,125,50)   Color::hsv(120, .6, .8)   Color::gray(.5f)
                               c.withAlpha(.4f)  c.lighter()  c.darker()  c.mix(other, t)
kPi  kTau  radians(deg)  degrees(rad)   // 对应 Processing 的 PI/TWO_PI；MSVC 没有 M_PI，别自己再写一份
```

## 数据与文件

```cpp
struct Node { std::string name; Vec2 pos; int trash = 0; };
EASEL_JSON(Node, name, pos, trash)        // 写在结构体下面，就有了存读能力

json j = node;              Node n = j.get<Node>();
fs::loadJson("data/x.json") // 读不到就返回 null，不抛异常
fs::saveJson("data/x.json", j)
file::open("工程文件", "json")             // 系统对话框，取消时返回空串
file::save("project.json")
```

## 随机（默认每次运行都不一样；种子永远打日志，`--seed N` 能复现）

```cpp
rng().i(1, 100)   rng().d(0, 1)   rng().chance(0.3)   rng().shuffle(v)
seed(12345)       current_seed()          // 命令行 --seed 12345 也能改；同时换掉 noise() 的种子
```

算法类工程想要「同一条命令永远同一个结果」：`cli::parse` 之后自己
`seed(EASEL_FIXED_SEED)`（`template/` 里已经这样写了；创意作品删掉这一行）。

## App

```cpp
int main(int argc, char** argv) {
    easel::App app(argc, argv);
    app.title("MySketch").size(1280, 800).theme(easel::Theme::Forest());

    app.onDraw ([&](easel::Canvas& c){ /* 每帧重画整张画布 */ });
    app.onPanel([&]{ /* 每帧重画右侧面板 */ });
    app.onClick([&](easel::Vec2 w, easel::Mouse b){ /* w 是世界坐标 */ });
    app.onDrag ([&](const easel::Drag& d){ /* d.start d.current d.delta d.velocity */ });
    app.onKey  ([&](easel::Key k){ if (k == easel::Key::Space) fire(); }); // 按下瞬间，只触发一次
    app.onFrame([&](double dt){
        if (app.keyDown(easel::Key::Left))  x -= speed * dt;  // 持续按住；Canvas 上也有同名的 c.keyDown(k)
        if (app.keyDown(easel::Key::Right)) x += speed * dt;
    });

    app.transport(tl);                       // 底部播放条
    app.welcome("标题", "一句话", []{ ... }); // 启动页
    app.onExportCase([]{ return json(state); }); // 「导出调试用例」要导出什么
    app.status("准备好了");                   // 状态栏文字
    app.statusBar(true);                     // 底部状态栏默认不显示，要用就开（F12 打开时也会临时出现）
    app.frameRate(60);                       // 帧率上限，默认就是 60；垂直同步失效时的兜底
    app.maxDelta(0.05);                      // dt 上限，默认就是 0.05；窗口被挡一下再回来，dt 不会飙成好几秒
    return app.run();
}
```

### 命令行参数——**这是你编出来的作品接受的参数，不是 `easel` 工作台的命令**

上面这段 `main` 编出来的可执行文件（`app.exe` / `app`，双击「编译并运行」时跑起来的那个子进程）
认这些参数；工作台自己的命令表（`--new` / `--build` / `--run` / `--package` / `--export` / `--doctor`）
在 [`reference.md`](reference.md) 和两份 `README` 里，两套命令别混着敲。

`你的作品 --open data/x.json --solve`（跳过点击） · `--doctor`（自检） ·
`--debug`（直接开调试台） · `--edit`（直接开编辑栏） · `--edit-run`（开起来就编译运行一次） ·
`--export <目录>`（导出可独立编译的工程，不开窗口） · `--seed N` ·
`--frames N --screenshot a.png`（截图；`N` 必须 **≥ 1**，`0` 或负数直接报错、退出码 2——
「一直跑」本来就不用加 `--frames`，「帧率不限制」是另一个参数 `--fps 0`，两者别搞混） ·
`--fps N`（帧率上限，`0` = 不限制） ·
`--warmup N`（先空转 N 帧只跑逻辑不渲染，再开始正常帧——截「要等几秒才发生」的效果配合 `--frames --screenshot` 用） ·
`--help`（列出全部参数；认不出的参数会报错退出，不会开窗口）

## 工作台 —— 从这里开始（D-29）

双击它（Windows：工具箱里的 `easel.bat`；Mac：`Easel`）。

| 按钮 | 干什么 |
|---|---|
| 新建工程… | 骨架下拉默认是**空白**：只有 `src/app.cpp`，画一个 Hello。选「算法骨架」才给 `src/solver.cpp` + 数据文件 + 回放 + 收敛曲线；也可以直接从一个自带示例建。作品名只能是英文 |
| 编译并运行（F5） | 编译 + 把作品跑起来（**作品是子进程，自己一个窗口**）。报错行点一下就跳到出错的代码 |
| 新建工程后（空白） | 目录里只有 `src/app.cpp`；脚手架都在 `.easel/` 里（不生成 README） |
| 想加测试 | 新建 `tests/test_solver.cpp`，第一行 `#include "../src/solver.cpp"`，下次编译自动带上 |
| 停止 | 掐掉正在跑的作品 |
| 生成 exe | Release 版 + assets/data + README.txt，放进 `dist/<名字>-release/`，可以直接双击 |
| 导出源码 | 自足的完整工程，不装 Easel、不联网也能编 |

发布包带着预编译好的 Easel（ImGui/GLFW/ImPlot 已经编过一遍），新建工程第一次编译只要几秒钟。
「几分钟」说的是另一件事——从源码树自己编 Easel 本身（工作台、库、依赖全部从头过一遍），
只有改 Easel 源码或者没用发布包的人才会遇到。往后不管哪种，改一行代码重编大约都是 6 秒。

## 编辑栏（F9）—— 兜底：机器上没有 VS Code 时改算法

| 键 | 干什么 |
|---|---|
| **F9** | 开 / 关左边这一栏（默认关着；发布出去的画面里不会有它） |
| **F5** | 保存 → `g++ src/solver.cpp` → 跑起来，输出落在下半屏 |
| **Ctrl+S** | 保存 |
| **Ctrl+滚轮** | 字号大小（中文比英文小一号，是控件的固定列宽换来的） |

- 编的是**命令行版**的 `solver`（`-DEASEL_STANDALONE`），不是这个界面程序本身。
- 报错那行是**可以点的**，直接跳到出错的位置，行号旁边留一个红标记，鼠标停上去看原文。
- 它不是 IDE：没有断点、没有补全、只开 `src/solver.cpp` 一个文件。
  要断点就用 VS Code 或小熊猫C++ —— 两边可以同时开着，
  外面改了文件，编辑栏会问你「读外面的」还是「留我的」，绝不偷偷覆盖。
- 找不到编译器时（双击 exe 启动，PATH 里没有工具箱），按钮会直接告诉你下一步怎么办。

## 导出工程（编辑栏右上角 / `app --export`）

出来的目录是**自足**的：拷到一台没装过任何东西、也不联网的机器上，

```
g++ -std=c++17 -DEASEL_STANDALONE src/solver.cpp -o solver    # 只要算法，一条命令
cmake -S . -B build && cmake --build build                    # 连界面一起，不用给任何参数
```

因为包里带着 `src/easel.hpp`（单头零依赖）、`easel/`（库源码）和 `easel/vendor/`
（全部第三方依赖的源码），顶层 `CMakeLists.txt` 会自己发现自带的 `easel/`。
导出完会写一份 `CHECK.txt`，逐条核对上面这些，并**真的编一次** `solver.cpp` 来验证。

## Canvas（位置是世界坐标，线宽和字号是像素）

```cpp
c.fill(color)  c.noFill()  c.stroke(color, 2.0)  c.noStroke()
c.alpha(0.5)   c.textSize(14)  c.dashed(6, 4)  c.solid()   c.push() / c.pop()

c.line(a, b);                 c.polyline(点数组, 是否闭合);
c.circle(中心, 世界半径);      c.dot(中心, 像素半径);      // dot 不随缩放变大
c.rect(Rect, 圆角像素);        c.text(位置, "字", Align::Center);   // 圆角像素不填=直角；有变换（push/rotate/scale）时圆角会被忽略
c.textWidth("字")             // 当前 textSize() 下这段字多宽（像素）——排版、右对齐/居中要用
c.image(tex, Rect);           // Texture tex = easel::loadTexture("assets/map.png");

c.toScreen(w)  c.toWorld(s)  c.world()  c.zoom()  c.mouse()  c.camera()
```

## 创意（Processing / Scratch 的对应物）

```cpp
// 变换栈：后调用的先作用在点上，translate(p).rotate(a) 是「先转，再挪过去」。
// 记得 push()/pop() 配对，不然变换会一直累积到下一次画东西上。
c.push().translate({x, y}).rotate(弧度).scale(s);
c.rect(box);
c.pop();

// 新图元：Processing 同名函数
c.triangle(a, b, c);  c.polygon(点数组);  c.ellipse(中心, rx, ry);
c.arc(中心, r, a0, a1, /*pie=*/false);    c.bezier(p0, p1, p2, p3);

// 自定义形状：beginShape/vertex/endShape，closed 决定填充还是折线
c.beginShape();
for (Vec2 p : pts) c.vertex(p);
c.endShape(true);

// 精灵子图：只画图片的一小块（Scratch 的「造型」），逐帧动画就是换 srcPx。像素风加 true：loadTexture("sheet.png", true)
c.image(sheet, 目标Rect, Rect(frame * 32, 0, 32, 32));
```

```cpp
// Layer —— Scratch 的画笔：落笔就留在那儿，Easel 每帧重画整张画布，
// 「不清」这件事必须自己记着，Layer 就是替你记的那个本子。
Layer ink;                                 // App 的成员变量，不要每帧新建
void onFrame(double dt) { if (c.hovered()) ink.stroke(色, 2).line(上一个点, c.mouse()); }
void onDraw(Canvas& c)  { c.draw(ink); }    // 每帧重放
ink.limit(3000);                           // 尾巴自动变短，超了丢最早的
ink.clear();                               // 全部擦除
```

```cpp
// Graphics —— 离屏画布：Layer 逐条重放扛不住的积累型效果（流场/涂鸦/长拖尾）用它，O(1)/帧
Graphics g;  app.onStart([&]{ g.create(1280, 800); });   // 像素尺寸；坐标系是像素，不受主画布相机影响
app.onFrame([&](double dt){ g.begin().stroke(色, 2).line(a, b); g.end(); });   // 只能在 onStart/onFrame 里 begin/end
app.onDraw([&](Canvas& c){ c.image(g.texture(), worldRect); });   // 整块贴到主画布；g.clear() 才会真的擦掉
```

```cpp
// noise() —— Processing 的柏林噪声：相邻的输入给相邻的输出，画出来是山脉、云、飘动的草
double n = noise(x * 0.05, t);             // noiseSeed(s) 换一片；noiseDetail(层数, 衰减)
double y = remap(n, 0, 1, 低, 高);          // 把 [0,1] 的噪声值搬到你要的范围（对应 Processing 的 map()）
```

```cpp
// audio:: —— Scratch 的声音积木
audio::play("assets/click.wav");           audio::loop("assets/bgm.mp3");
audio::stop();                             audio::volume(0.5);
double lv = audio::loudness();             // 放出去的响度 0..1（不是麦克风）
std::vector<float> f = audio::spectrum(32);// 频谱，画成柱子就是音乐可视化

audio::Sound beep = audio::load("assets/beep.wav");   // 短音效反复用：load 一次，play 很多次
beep.volume(0.3);  beep.pitch(1.5);  beep.play();
```

## Camera

```cpp
app.camera().fit(Rect::bounding(所有点));   // 一键装进画布
app.camera().center({0,0});   app.camera().zoom(20);   app.camera().panZoom(false);
app.camera().scaleBar(&project.unitScale, "米");      // 左下角比例尺（单位名随你写）
```

滚轮缩放 · 中键（或空格 + 左键）拖拽平移。

## Timeline（算完 + 回放）

```cpp
easel::Timeline<Frame> tl;
tl.load(solve(project, params));    // 一次算完，返回全过程
tl.current()  tl.at(i)  tl.frames()  tl.size()  tl.index()
tl.play()  tl.pause()  tl.step(±1)  tl.seek(i)  tl.speed(2.0)  tl.fps(30)  tl.loop = true;
```

## ui:: （每个都只有十几行，包的就是 ImGui）

```cpp
if (ui::section("参数")) { ... }
ui::slider("温度", &T0, 1.0, 5000.0);   ui::slider("点数", &n, 5, 50);
ui::sliderCommit("温度", &T0, 1.0, 5000.0);   // 只在松手那一帧返回 true；参数改动要重算时用它，别用 slider
ui::toggle("显示名称", &show);           ui::button("开始优化", true /*占满宽度*/)
ui::select("算法", &algo, {"冒泡排序", "选择排序"});   // N 选一，*v 是选中项下标，返回值同 slider
ui::input("文字", &text);          // 单行文字输入，v 是 std::string*，每敲一个字符就返回 true
ui::inputCommit("种子", &seedText); // 和 sliderCommit 对 slider 的关系一样，回车/失焦才返回 true
ui::stat("总里程", 4.72, "公里", 2);     ui::chart("收敛", ys, "总长度");
ui::title("小标题")  ui::help("灰色说明")  ui::separator()  ui::spacing()  ui::sameLine()
```

**没有的直接调 ImGui**：`ImGui::Combo` / `ImGui::TextWrapped` / `ImGui::BeginTable` …
`#include <easel/easel.h>` 已经把 `imgui.h`、`implot.h` 带上了。
F12 → 帮助 里能打开 ImGui 和 ImPlot 的示例窗口，那是活的手册。

## Theme（五个预设，一行切换）

```cpp
Theme::Forest()  Theme::Ocean()  Theme::Ember()  Theme::Paper()  Theme::Slate()
t.accent  t.accent2  t.bg  t.surface  t.fg  t.muted  t.good  t.warn  t.bad  t.radius
t.fontPath = "assets/fonts/我的字体.ttf";   // 不设就自动找系统中文字体
```

## Easel 故意没有的东西

找不到就是真没有，别再翻头文件——下面这些目前都没做，绕过去的办法一起写了：

| 没有 | 绕过去的办法 |
|---|---|
| 混合模式（加法混合 / 正片叠底之类，只有普通的透明叠加） | 发光/叠亮效果用半透明多画几层：`c.alpha(0.3)` 反复画同一个图形 |
| 渐变填充（`fill()` 只认纯色） | 贴一张渐变图当纹理用 `c.image()`，或者自己分段画多个纯色图形模拟 |
| 裁剪 / 遮罩（clip region） | 没有画布级裁剪；ImGui 的 `PushClipRect` 能顶一部分场合 |
| 视频播放（只有图片和精灵表） | 序列帧图片当动画放，或者用 `Graphics` 离屏画布自己合成 |
| 内置粒子系统 | 自己写一个 `std::vector<Particle>`，`onFrame` 里更新、`onDraw` 里画 |

`frameCount` / `millis()` 这类计时器还在做，不在这张表里——真没有的时候才信这张表。

---

# 第二页 · 出问题了怎么办

## 四个宏（同一行代码，命令行和界面里都能用）

| 写法 | 命令行里 | 界面里 |
|---|---|---|
| `EASEL_LOG("第 %d 轮", i)` | 打到终端 | 日志窗（带 file:line、帧号） |
| `EASEL_TRACE("长度", cur.len)` | 打一行 CSV | **自动画成折线图** |
| `EASEL_CHECK(条件, "说明")` | 立刻停住 + 调用栈 | 红色横幅 + 暂停回放，**不崩** |
| `easel::dbg("当前解", x)` | 什么都不做 | F12 → 状态，实时显示 |

还有 `EASEL_WARN` / `EASEL_ERROR` / `EASEL_CHECK_EQ(a,b)` / `EASEL_CHECK_NEAR(a,b,1e-6)`。

`cout` 和 `printf` 随便写：命令行在终端看，界面里在日志窗看
（Windows 上 GUI 程序吞输出的老问题，Easel 已经接管解决了）。

## 状态灯

状态栏最左边那个点：**绿**=一切正常 · **黄**=有警告 / 没有中文字体 · **红**=断言失败或崩溃过。
直接按 **F12** 打开调试台（状态栏默认不显示，F12 打开时会一并出现）。

## 调试台六页

| 页 | 回答的问题 |
|---|---|
| **日志** | 我打的东西去哪了（级别 / 过滤 / 重复折叠 / 一键复制） |
| **追踪** | 这个数是怎么变的（`EASEL_TRACE` 自动成图） |
| **状态** | 现在到底是什么（`dbg()` 实时表格 + 迷你折线） |
| **画布** | 为什么什么都没画出来（图元数 / 视口外 / 看不见的 / 世界范围） |
| **用例** | 怎么把问题带回单文件（导出 + 复现命令 + `debug/` 列表） |
| **自检** | 环境有没有问题（后端 / 显卡 / DPI / 字体 / 种子 / 工作目录 / 编译器） |

## 那条闭环（整个设计就是为了它）

```
界面里看到不对
   → F12 → 用例 → 「导出调试用例」        得到 debug/case-003.json + 一行命令
   → 在终端里跑那行命令                    ./solver --case debug/case-003.json
   → 用你最熟悉的方式定位（printf、断点、单步）
   → 改 solver.cpp
   → ctest --test-dir build/default -C RelWithDebInfo   确保没改坏别的（-C for Windows MSVC）
   → 回到界面看                             app --open ... --solve
```

**能这么干的前提是把种子一起带上**：不加 `--seed` 时每次运行都会换一颗新种子，结果
也跟着变——`--case` 导出的用例文件本身记录着当时的种子，跑那条现成命令自然就是同一次；
只有 `--seed N` 复现某一次时，`N` 必须是日志里打出来的那个数，不是随便一个固定值。

## 对拍器

```cpp
auto gen  = [](easel::Rng& r){ /* 造一组随机输入 */ return input; };
auto fast = [](const Input& in){ /* 你写的快算法 */ return ans; };
auto slow = [](const Input& in){ /* 笨但一定对的  */ return ans; };
auto rep = EASEL_CROSSCHECK(200, gen, fast, slow);   // 不一致就存 debug/duipai-001.json
```

第 k 轮用的种子是 `起始种子 + k`，所以出错那一轮可以单独复现。

## 常见毛病

| 现象 | 多半是 |
|---|---|
| 画布一片空白 | F12 → 画布：图元数是 0？onDraw 提前 return 了。全在视口外？`camera().fit(...)` |
| 中文变方框 | F12 → 自检看「中文字形」。设 `Theme::fontPath` 或环境变量 `EASEL_FONT` |
| 点了没反应 | 鼠标在面板上时不会触发 `onClick`；按住空格是平移不是点击 |
| 越界了但不报错 | 用 `cmake --preset debug`（开 ASan + 标准库检查），越界当场就停 |
| 崩了看不到栈 | VS Code 里按 F5（配置 1），或者用 debug 预设 |
| 改了 `solver.cpp`，App 里没变 | 三个 target 各自 include 它一次，重新构建就好（不是热重载） |
| 窗口开不起来 | `app --doctor` 看后端和显卡；Windows 上可以试 DX11 后端 |

## 三条纪律

1. **`onDraw` 里不要打断点** —— 窗口会冻住。用 `easel::dbg()`。
2. **`solve()` 是纯函数** —— 一次算完返回全过程，界面只负责回放。别在界面里改算法状态。
3. **`debug/` 目录就是你的 bug 日志** —— 每个存下来的用例都是一个曾经出过的错，别删。

---

Easel v0.1.1 · 代码酷 daimaku.net · MIT
