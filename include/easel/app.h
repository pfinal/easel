// Easel — app.h  窗口、主循环、布局、调试台
#ifndef EASEL_APP_H
#define EASEL_APP_H

#include <easel/camera.h>
#include <easel/canvas.h>
#include <easel/core.h>
#include <easel/theme.h>
#include <easel/timeline.h>

namespace easel {

enum class Mouse { Left, Right, Middle };

// 键盘按键。数值直接等于对应的 ImGuiKey（src/app.cpp 里给这里每一项都写了
// static_assert，ImGui 哪天改了数值顺序，编译立刻炸，不会悄悄错位）。这样定有两个
// 好处：Key <-> ImGuiKey 的转换零成本（直接 (ImGuiKey)k）；这里没列出的冷门键
// （小键盘、F13 往上……）依然能强转成 Key 塞进来用，不会因为「没在枚举里」就用不了。
// 只收学生真的会用的键；鼠标键不在这儿，那是 Mouse 的地盘。
enum class Key {
    // 字母
    A = 546, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    // 数字（标识符不能以数字开头，前面加 Num）
    Num0 = 536, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    // 方向键
    Left = 513, Right, Up, Down,
    // 常用控制键
    Tab = 512, Space = 524, Enter = 525, Escape = 526, Backspace = 523, Delete = 522,
    LeftShift = 528, RightShift = 532, LeftCtrl = 527, RightCtrl = 531, LeftAlt = 529, RightAlt = 533,
    // 功能键
    F1 = 572, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

struct Drag {
    Vec2  start;        // 世界坐标：按下的地方
    Vec2  current;      // 世界坐标：现在
    Vec2  delta;        // 世界坐标：这一帧移动了多少
    Vec2  velocity;      // 世界单位/秒，松手那一帧也有效（"甩出去"用它）。
                          // ended 帧 delta 会被清零，velocity 不会——它记的是松手前
                          // 最近几帧的平均速度，即使松手前正好停顿了一帧也不会掉到 0。
    Mouse button = Mouse::Left;
    bool  began = false;
    bool  ended = false;
};

// 一个 App 就是一个窗口。main 里把回调接上，最后 run()。
//
//     int main(int argc, char** argv) {
//         easel::App app(argc, argv);
//         app.title("MySketch").size(1280, 800).theme(easel::Theme::Forest());
//         app.onDraw([](easel::Canvas& c){ ... });
//         app.onPanel([]{ ... });
//         return app.run();
//     }
class App {
public:
    App(int argc = 0, char** argv = nullptr);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // ---------------- 设置 ----------------
    App&         title(const std::string& t);
    App&         size(int w, int h);
    App&         theme(const Theme& t);
    const Theme& theme() const;
    App&         background(const Color& c);   // 不给就用主题的 bg
    App&         panelWidth(float px);
    // 帧率上限，默认 60。传 0 = 不限制（完全交给垂直同步）。垂直同步在虚拟机 /
    // 没装驱动的机器上经常失效，不限帧就会空转烧一个 CPU 核，所以默认给个兜底。
    // `--fps N` 命令行参数会在这句话之后覆盖这里设的值。
    App&         frameRate(double fps);
    // dt 的上限（秒），默认 0.05（20fps 的一帧）。窗口被挡一下、切到后台再切回来，
    // 现实里过去的时间可能是好几秒——不夹住的话粒子/物理模拟会一帧瞬移到很远的地方。
    // 传 0 = 不夹（信任 dt 有多大就是多大，调试单步之类的场景可能想要这样）。
    App&         maxDelta(double seconds);
    // 工具类程序专用：开启后，如果这一帧没有任何鼠标/键盘输入，且已经空闲超过
    // 0.5 秒，就把这一帧的目标间隔放大到 100ms（10 帧）省电；一有输入、或子进程在
    // 跑（编辑栏编译/运行）、或有 toast/横幅在显示，立刻恢复满速。默认关（作品的
    // 画布可能有自己的动画，不该被这个功能打断）；工作台这种界面静止时用得上。
    App&         idleThrottle(bool on);
    // 开了 idleThrottle 时，这个回调返回 true 就保持满帧（比如有子进程在输出）。
    App&         busyWhen(std::function<bool()> fn);

    // ---------------- 回调（每帧调用，立即模式）----------------
    App& onDraw(std::function<void(Canvas&)> fn);   // 画布
    App& onPanel(std::function<void()> fn);         // 右侧面板
    App& onClick(std::function<void(Vec2, Mouse)> fn);
    App& onDrag(std::function<void(const Drag&)> fn);
    App& onKey(std::function<void(Key)> fn);         // 按下瞬间触发一次（持续按住用 keyDown）
    App& onFrame(std::function<void(double)> fn);   // 每帧最先调用，参数是 dt（秒）
    // 窗口和显卡都准备好之后调一次。加载图片（loadTexture）必须放在这里，
    // 不能放在 run() 之前 —— 那时候还没有显卡上下文。
    App& onStart(std::function<void()> fn);
    // 画布那块区域整个交给你，自己用 ImGui 画（工具类程序用得上：没有画布，只有界面）。
    // 参数是那块区域的屏幕矩形。和 onDraw 可以同时用，onWindow 画在上面。
    App& onWindow(std::function<void(const Rect&)> fn);

    // ---------------- 输入查询 ----------------
    // 这一帧某键是否按住（方向键控制角色移动这类「持续按着」的输入用它；
    // 「按下的瞬间」用上面的 onKey，两者不重复，别都试一遍）。Canvas 上也有一份同名的，
    // onDraw 里拿着 Canvas& 就不用再去捕获 app。
    bool keyDown(Key k) const;

    // ---------------- 启动页（D-21：别让评委看到空白画布）----------------
    App& welcome(const std::string& title, const std::string& desc, std::function<void()> body);
    void closeWelcome();
    bool welcomeOpen() const;

    // ---------------- 播放条 ----------------
    App& transport(TimelineBase& tl);

    // ---------------- 状态栏 ----------------
    void status(const std::string& s);
    void toast(const std::string& s);           // 顶部中间飘一条，两秒半后消失
    // 底部状态栏（状态灯、状态文字、缩放倍数）。默认不显示；F12 调试台打开时会临时出现。
    App& statusBar(bool on);

    // ---------------- 编辑栏（D-28）----------------
    // F9 打开左边那一栏：改 src/solver.cpp，F5 编译并运行命令行版。
    // 默认关着 —— 交付给评委的时候画面里不该有一个 IDE。
    App&        editorFile(const std::string& path);   // 不给就自动找 src/solver.cpp
    App&        editorOpen(bool on);
    bool        editorOpen() const;
    App&        editorWidth(float px);
    App&        editorEnabled(bool on);   // false = 连 F9 都不响应（工具类程序不需要它）
    App&        debugConsoleEnabled(bool on);   // false = 连 F12 都不响应（工具类程序不需要调试台）
    // 导出一个「离了 Easel 也完整」的工程（命令行等价物：app --export <目录>）
    bool        exportProject(const std::string& outDir = {}, const std::string& name = {});

    // ---------------- 调试 ----------------
    // 让「导出调试用例」按钮知道要导出什么。不接的话按钮是灰的。
    App&        onExportCase(std::function<json()> fn);
    std::string exportDebugCase(const json& state, const std::string& note = {});
    App&        debugConsoleOpen(bool on);
    bool        debugConsoleOpen() const;
    // 把当前画面存成 PNG（写文档、录视频、CI 里看一眼都用得上）。
    // 命令行也能用：app --frames 60 --screenshot shot.png —— 那条路存的一定是最后那一帧
    // 画好的画面（截图动作卡在「画完、还没交换缓冲区」的那一刻）。
    // 在 onDraw / onPanel 里手动调它则不然：那时候这一帧还没画完、还没呈现，读到的是
    // 上一次交换留在后台缓冲里的画面（也就是慢一两帧）。要「所见即所得」就用命令行那条路。
    bool        screenshot(const std::string& path);

    // ---------------- 命令行 ----------------
    //   -h, --help         打印这张表然后退出（不开窗口；构造 App 时就处理掉了）
    //   --open <文件>      启动时打开它（用 openPath() 取）
    //   --solve            打开之后直接算（用 wantsSolve() 判断）
    //   --seed N           换随机种子（不给就永远是同一个结果）
    //   --case <文件>      载入一个调试用例
    //   --doctor           打印环境自检然后退出，不开窗口
    //   --debug            启动就把 F12 调试台打开
    //   --edit             启动就把左边的编辑栏打开
    //   --edit-run         打开编辑栏并立刻编译运行一次（CI 用；别和工作台自己的 --run 搞混）
    //   --export [目录]    导出一个能独立编译的完整工程，然后退出（不开窗口）
    //   --quiet            EASEL_TRACE / EASEL_LOG 不往终端刷屏
    //   --frames N         跑 N 帧自动退出（CI / 截图用）。N 必须 ≥ 1；--frames 0 是
    //                      非法值（会报错退出），「不限制」是 --fps 0 那个参数的意思
    //   --screenshot <png> 退出前存一张截图
    //   --fps N            帧率上限（0 = 不限制），覆盖 frameRate() 设的值
    //   --warmup N         进入正常循环前先空转 N 帧（只调 onFrame(dt)，dt 固定 1/60，
    //                      不渲染）；截「要等几秒才发生」的效果时配合 --frames/--screenshot 用
    // 自己的参数用 easel::cli::args() 取：cli::args().num("iters", 200)
    std::string openPath() const;    // --open 给的路径，没给就是空串
    bool        wantsSolve() const;  // 有没有 --solve

    // ---------------- 其它 ----------------
    Camera&      camera();
    Canvas&      canvas();
    // 现在是第几帧（Processing 的 frameCount）。第一帧的回调里就是 1，之后每帧 +1。
    // --warmup N 预热的那 N 帧也算在里面（预热的就是「帧」）；--frames N 数的是画出来的
    // 帧，两者在有 --warmup 时会差一个 N。想让动画「每 30 帧换一次颜色」直接拿它取模。
    long long    frameCount() const;
    // 从启动到现在多少秒（Processing 的 millis()，只是单位是秒）。同一帧里多次调用
    // 返回同一个值——帧开头取一次真实时钟，整帧都用它，动画不会因为一帧内取两次时间
    // 而错位。--warmup 不会推进它（预热是一瞬间跑完的，它记的是真实时钟）。
    double       elapsed() const;
    double       dpiScale() const;
    const char*  backendName() const;
    std::string  doctor() const;      // 完整自检（core + 后端 + 字体 + DPI）
    void         quit();
    int          run();

    static App* instance();

    struct Impl;

private:
    Impl* p_;
    // 主循环每帧的内容（run() 里 while 循环体）。网页版（Emscripten）会把它接到
    // emscripten_set_main_loop 上，所以它必须是一个能反复单独调用的函数。
    void frame();
};

}  // namespace easel
#endif
