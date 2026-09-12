// Easel — app.cpp  窗口、主循环、布局、状态灯
#include "internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <cstdio>
#endif

namespace easel {

#if defined(_WIN32)
// GUI 子系统的程序（WIN32_EXECUTABLE TRUE）没有控制台；从命令行跑 --doctor / --export
// 时想把输出接回父终端，才需要这一步。但有两种情况*不*该去动 stdout：
//   1. 已经被重定向到文件/管道（`easel --doctor > out.txt`，或者 CI 里
//      `app.exe --doctor > doctor.txt 2>&1`）——这时 GetStdHandle(STD_OUTPUT_HANDLE)
//      有效、GetFileType 不是 FILE_TYPE_UNKNOWN，stdout 本来就能写，freopen 反而会
//      把它从文件改接到 CONOUT$，重定向就丢了。
//   2. 压根没有父控制台（双击启动）：AttachConsole 会失败，什么都不做，不弹黑框。
// 只有 stdout 无效（没有句柄，也没有被重定向）且 AttachConsole 成功时，才 freopen 到
// CONOUT$ —— 也就是「从命令行/终端跑起来，但因为是 WINDOWS 子系统而没继承到控制台」这一种。
static void attachParentConsole() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != NULL && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN) {
        // 已经有效（重定向到文件/管道，或者继承了控制台）：什么都不做，保住它。
        return;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE* f = nullptr;
#if defined(_MSC_VER)
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$",  "r", stdin);
#else
    f = freopen("CONOUT$", "w", stdout);
    f = freopen("CONOUT$", "w", stderr);
    f = freopen("CONIN$",  "r", stdin);
#endif
    (void)f;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetConsoleOutputCP(CP_UTF8);   // 我们打的是 UTF-8
}
#endif

using internal::col;
using internal::iv;
using internal::iv4;
using internal::shared;

// ============================================================================
//  Key <-> ImGuiKey 数值对齐检查
//  app.h 里 Key 的每一项都是手写的字面数字（那份公开头文件不能 #include <imgui.h>），
//  这里逐一 static_assert 保证它们和 ImGui 当前的 ImGuiKey 真的对得上。ImGui 升级版本
//  要是改了某个键的数值，这里编译期就炸，不会等到运行时才发现按键全错位了。
// ============================================================================
static_assert((int)Key::A == ImGuiKey_A, "Key::A != ImGuiKey_A");
static_assert((int)Key::B == ImGuiKey_B, "Key::B != ImGuiKey_B");
static_assert((int)Key::C == ImGuiKey_C, "Key::C != ImGuiKey_C");
static_assert((int)Key::D == ImGuiKey_D, "Key::D != ImGuiKey_D");
static_assert((int)Key::E == ImGuiKey_E, "Key::E != ImGuiKey_E");
static_assert((int)Key::F == ImGuiKey_F, "Key::F != ImGuiKey_F");
static_assert((int)Key::G == ImGuiKey_G, "Key::G != ImGuiKey_G");
static_assert((int)Key::H == ImGuiKey_H, "Key::H != ImGuiKey_H");
static_assert((int)Key::I == ImGuiKey_I, "Key::I != ImGuiKey_I");
static_assert((int)Key::J == ImGuiKey_J, "Key::J != ImGuiKey_J");
static_assert((int)Key::K == ImGuiKey_K, "Key::K != ImGuiKey_K");
static_assert((int)Key::L == ImGuiKey_L, "Key::L != ImGuiKey_L");
static_assert((int)Key::M == ImGuiKey_M, "Key::M != ImGuiKey_M");
static_assert((int)Key::N == ImGuiKey_N, "Key::N != ImGuiKey_N");
static_assert((int)Key::O == ImGuiKey_O, "Key::O != ImGuiKey_O");
static_assert((int)Key::P == ImGuiKey_P, "Key::P != ImGuiKey_P");
static_assert((int)Key::Q == ImGuiKey_Q, "Key::Q != ImGuiKey_Q");
static_assert((int)Key::R == ImGuiKey_R, "Key::R != ImGuiKey_R");
static_assert((int)Key::S == ImGuiKey_S, "Key::S != ImGuiKey_S");
static_assert((int)Key::T == ImGuiKey_T, "Key::T != ImGuiKey_T");
static_assert((int)Key::U == ImGuiKey_U, "Key::U != ImGuiKey_U");
static_assert((int)Key::V == ImGuiKey_V, "Key::V != ImGuiKey_V");
static_assert((int)Key::W == ImGuiKey_W, "Key::W != ImGuiKey_W");
static_assert((int)Key::X == ImGuiKey_X, "Key::X != ImGuiKey_X");
static_assert((int)Key::Y == ImGuiKey_Y, "Key::Y != ImGuiKey_Y");
static_assert((int)Key::Z == ImGuiKey_Z, "Key::Z != ImGuiKey_Z");
static_assert((int)Key::Num0 == ImGuiKey_0, "Key::Num0 != ImGuiKey_0");
static_assert((int)Key::Num1 == ImGuiKey_1, "Key::Num1 != ImGuiKey_1");
static_assert((int)Key::Num2 == ImGuiKey_2, "Key::Num2 != ImGuiKey_2");
static_assert((int)Key::Num3 == ImGuiKey_3, "Key::Num3 != ImGuiKey_3");
static_assert((int)Key::Num4 == ImGuiKey_4, "Key::Num4 != ImGuiKey_4");
static_assert((int)Key::Num5 == ImGuiKey_5, "Key::Num5 != ImGuiKey_5");
static_assert((int)Key::Num6 == ImGuiKey_6, "Key::Num6 != ImGuiKey_6");
static_assert((int)Key::Num7 == ImGuiKey_7, "Key::Num7 != ImGuiKey_7");
static_assert((int)Key::Num8 == ImGuiKey_8, "Key::Num8 != ImGuiKey_8");
static_assert((int)Key::Num9 == ImGuiKey_9, "Key::Num9 != ImGuiKey_9");
static_assert((int)Key::Left == ImGuiKey_LeftArrow, "Key::Left != ImGuiKey_LeftArrow");
static_assert((int)Key::Right == ImGuiKey_RightArrow, "Key::Right != ImGuiKey_RightArrow");
static_assert((int)Key::Up == ImGuiKey_UpArrow, "Key::Up != ImGuiKey_UpArrow");
static_assert((int)Key::Down == ImGuiKey_DownArrow, "Key::Down != ImGuiKey_DownArrow");
static_assert((int)Key::Tab == ImGuiKey_Tab, "Key::Tab != ImGuiKey_Tab");
static_assert((int)Key::Space == ImGuiKey_Space, "Key::Space != ImGuiKey_Space");
static_assert((int)Key::Enter == ImGuiKey_Enter, "Key::Enter != ImGuiKey_Enter");
static_assert((int)Key::Escape == ImGuiKey_Escape, "Key::Escape != ImGuiKey_Escape");
static_assert((int)Key::Backspace == ImGuiKey_Backspace, "Key::Backspace != ImGuiKey_Backspace");
static_assert((int)Key::Delete == ImGuiKey_Delete, "Key::Delete != ImGuiKey_Delete");
static_assert((int)Key::LeftShift == ImGuiKey_LeftShift, "Key::LeftShift != ImGuiKey_LeftShift");
static_assert((int)Key::RightShift == ImGuiKey_RightShift, "Key::RightShift != ImGuiKey_RightShift");
static_assert((int)Key::LeftCtrl == ImGuiKey_LeftCtrl, "Key::LeftCtrl != ImGuiKey_LeftCtrl");
static_assert((int)Key::RightCtrl == ImGuiKey_RightCtrl, "Key::RightCtrl != ImGuiKey_RightCtrl");
static_assert((int)Key::LeftAlt == ImGuiKey_LeftAlt, "Key::LeftAlt != ImGuiKey_LeftAlt");
static_assert((int)Key::RightAlt == ImGuiKey_RightAlt, "Key::RightAlt != ImGuiKey_RightAlt");
static_assert((int)Key::F1 == ImGuiKey_F1, "Key::F1 != ImGuiKey_F1");
static_assert((int)Key::F2 == ImGuiKey_F2, "Key::F2 != ImGuiKey_F2");
static_assert((int)Key::F3 == ImGuiKey_F3, "Key::F3 != ImGuiKey_F3");
static_assert((int)Key::F4 == ImGuiKey_F4, "Key::F4 != ImGuiKey_F4");
static_assert((int)Key::F5 == ImGuiKey_F5, "Key::F5 != ImGuiKey_F5");
static_assert((int)Key::F6 == ImGuiKey_F6, "Key::F6 != ImGuiKey_F6");
static_assert((int)Key::F7 == ImGuiKey_F7, "Key::F7 != ImGuiKey_F7");
static_assert((int)Key::F8 == ImGuiKey_F8, "Key::F8 != ImGuiKey_F8");
static_assert((int)Key::F9 == ImGuiKey_F9, "Key::F9 != ImGuiKey_F9");
static_assert((int)Key::F10 == ImGuiKey_F10, "Key::F10 != ImGuiKey_F10");
static_assert((int)Key::F11 == ImGuiKey_F11, "Key::F11 != ImGuiKey_F11");
static_assert((int)Key::F12 == ImGuiKey_F12, "Key::F12 != ImGuiKey_F12");

// ============================================================================
//  Timeline 的播放逻辑（放这里是因为它要和主循环的 dt 对上）
// ============================================================================
void TimelineBase::seek(int i) {
    int n = (int)size();
    if (n == 0) { pos_ = 0; return; }
    pos_ = clamp(i, 0, n - 1);
}

void TimelineBase::update(double dt) {
    if (!playing_) return;
    int n = (int)size();
    if (n <= 1) { playing_ = false; return; }
    pos_ += dt * fps_ * speed_;
    if (pos_ >= n - 1) {
        if (loop) {
            pos_ = 0;
        } else {
            pos_ = n - 1;
            playing_ = false;
        }
    }
}

// ============================================================================
//  App::Impl
// ============================================================================
namespace {
App* g_instance = nullptr;
}

struct App::Impl {
    GLFWwindow* win = nullptr;
    std::string title = "Easel 应用";
    int         w = 1280, h = 800;
    Theme       theme = Theme::Forest();
    bool        customBg = false;
    Color       bg;
    float       panelW = 340.f;
    float       dpi = 1.f;
    Camera      cam;
    Canvas      canvas;

    std::function<void(Canvas&)>     onDraw;
    std::function<void()>            onPanel;
    std::function<void(Vec2, Mouse)> onClick;
    std::function<void(const Drag&)> onDrag;
    std::function<void(Key)>         onKey;
    std::function<void(double)>      onFrame;
    std::function<void()>            onStart;
    std::function<void(const Rect&)> onWindow;
    std::function<json()>            exportProvider;

    bool          welcomeOpen = false, welcomeRequested = false;
    std::string   welTitle, welDesc;
    std::function<void()> welBody;

    TimelineBase* tl = nullptr;

    std::string statusText;
    std::string toastText;
    double      toastUntil = 0;

    bool   debugOpen = false;
    bool   statusBar = false;   // 底部状态栏：默认关，F12 调试台打开时临时出现
    bool   editorOpen = false;
    bool   editorEnabled = true;
    bool   debugConsoleEnabled = true;
    bool   runOnce = false;
    float  editorW = 560.f;
    bool   showImGuiDemo = false;
    bool   running = true;
    double lastTime = 0;
    // dt 的上限（秒）：窗口被挡一下、切到后台再切回来，现实里过去的时间可能是好几秒，
    // 不夹住的话粒子/物理模拟会一帧瞬移到很远的地方。0 = 不夹。
    double maxDeltaSeconds = 0.05;   // 默认 20fps 的一帧

    // 帧率上限 / 省电（D-3x：垂直同步在虚拟机上常常失效，空转烧一个 CPU 核）
    double targetFps = 60.0;    // 0 = 不限制
    double frameInterval = 1.0 / 60.0;   // 这一帧结束后，run() 的循环要 sleep 到这个间隔
    bool   idleThrottleOn = false;
    double lastActiveTime = 0;  // 上一次「有事」（输入 / 子进程在跑 / toast / 横幅）的时间点
    std::function<bool()> busyFn;  // idleThrottle 的补充回调：返回 true 表示有子进程在忙

    // run() 循环体（现在是 frame()）用到的、每帧都要保留状态的局部变量
    ImGuiIO*    io = nullptr;
    Color       clear;
    long long   maxFrames = -1;
    std::string shotPath;
    long long   framesRun = 0;

    // 画布拖拽
    bool  dragging = false;
    Drag  drag;
    Vec2  pressScreen;
    bool  pressed = false;
    Mouse pressBtn = Mouse::Left;
    // Drag::velocity 的平滑：存最近 3 帧的瞬时速度（delta/dt），取平均——避免松手前
    // 恰好停顿一帧导致算出来的速度是 0，「甩出去」的手感会很奇怪。
    Vec2  dragVelHist[3];
    int   dragVelN = 0;

    Rect canvasRect{0, 0, 1, 1};
    bool themeDirty = false;
};

// ---------------------------------------------------------------- 构造
namespace {

// `app --help`：作品的**框架级**参数。学生自己定义的参数（cli::args().num("pad", 40)）
// 列不出来——框架根本不知道有哪些——所以这份用法只说框架给的这几个，末尾点一句
// 「作品自己的参数照样能用」。工作台的用法在 workbench/main.cpp，是另一套。
std::string appUsage(const std::string& prog) {
    std::string p = prog.empty() ? "app" : prog;
    return "用法：" + p + " [参数]\n"
           "\n"
           "Easel 作品的通用参数（框架提供，每个作品都有）：\n"
           "  -h, --help          打印这份用法然后退出\n"
           "  --doctor            打印环境自检（显卡 / 字体 / 编译器）然后退出，不开窗口\n"
           "  --frames N          画满 N 帧就自动退出（N ≥ 1；CI、截图用）\n"
           "  --screenshot <png>  退出前把最后一帧存成 PNG（和 --frames 一起用）\n"
           "  --warmup N          进主循环之前先空转 N 帧：只调 onFrame(dt)，dt 固定 1/60，不渲染\n"
           "  --fps N             帧率上限，0 = 不限制（覆盖代码里 frameRate() 设的值）\n"
           "  --seed N            固定随机种子（不给的话每次都不一样，用的种子会打在日志第一行）\n"
           "  --quiet             EASEL_LOG / EASEL_TRACE 不往终端刷屏\n"
           "  --debug             启动就把 F12 调试台打开\n"
           "  --edit              启动就把左边的编辑栏打开（平时按 F9）\n"
           "  --edit-run          打开编辑栏并立刻编译运行一次（CI 用）\n"
           "  --export [目录]     导出一个能独立编译的完整工程，然后退出，不开窗口\n"
           "  --open <文件>       启动时打开这个文件（作品里用 app.openPath() 取）\n"
           "  --solve             启动就直接算一遍（作品里用 app.wantsSolve() 判断）\n"
           "  --case <文件>       载入一个调试用例\n"
           "\n"
           "例子：\n"
           "  " + p + " --frames 1 --screenshot shot.png          画一帧、存图、退出\n"
           "  " + p + " --warmup 600 --frames 1 --screenshot late.png   先空转 600 帧再截图\n"
           "  " + p + " --doctor                                  环境自检\n"
           "\n"
           "作品自己的参数不在这张表里，但照样能用，也不会被拒绝：\n"
           "  cli::args().num(\"pad\", 40) / .str(\"file\") / .has(\"fast\")\n";
}

}  // namespace

App::App(int argc, char** argv) : p_(new Impl) {
#if defined(_WIN32)
    attachParentConsole();
#endif
    g_instance = this;
    // --help / -h 在 cli::parse() 之前处理：这样「本次随机种子」那行日志不会混进用法里，
    // 也不用等到 run()（学生的 main 里可能先 solve() 算半天才 run()）。
    //
    // 注意这里**只**认 --help，不做「认不出的参数就报错」那一套：作品会自己定义参数
    // （cli::args().num("pad", 40)），框架无从知道哪些名字是合法的，一旦拒绝未知参数就
    // 等于把学生的作品打死。工作台（workbench/main.cpp）的参数表是固定的、由我们自己
    // 维护，那边才做未知参数检查。
    for (int i = 1; i < argc && argv; ++i) {
        std::string a = argv[i] ? argv[i] : "";
        if (a == "--help" || a == "-h") {
            std::printf("%s", appUsage(internal::baseName(argc > 0 && argv[0] ? argv[0] : "app")).c_str());
            std::fflush(stdout);
            std::exit(0);
        }
    }
    if (argc > 0 && argv) cli::parse(argc, argv);
    installCrashHandler();
    // 从这一行起，EASEL_LOG / EASEL_TRACE / dbg 就往调试台里存了 ——
    // 哪怕 solve() 是在 app.run() 之前跑的，它的曲线也不会丢。
    // 但 EASEL_CHECK 还是走命令行那套（窗口都没有，红条给谁看）。
    internal::installHooks(false);
    if (!cli::args().str("case").empty()) {
        EASEL_LOG("命令行带了 --case %s", cli::args().str("case").c_str());
    }
}

App::~App() {
    internal::audioShutdown();
    internal::removeHooks();
    delete p_;
    if (g_instance == this) g_instance = nullptr;
}

App* App::instance() { return g_instance; }

// ---------------------------------------------------------------- 设置
App& App::title(const std::string& t) {
    p_->title = t;
    if (p_->win) glfwSetWindowTitle(p_->win, t.c_str());
    return *this;
}
App& App::size(int w, int h) { p_->w = w; p_->h = h; return *this; }
App& App::theme(const Theme& t) {
    p_->theme = t;
    // 窗口已经开着的话不能立刻换：这一帧可能正画到一半，清字体图集会崩。
    // 记个标记，下一帧开头再换。（学生在面板里放主题下拉框是很正常的做法。）
    if (p_->win) p_->themeDirty = true;
    return *this;
}
const Theme& App::theme() const { return p_->theme; }
App& App::background(const Color& c) { p_->customBg = true; p_->bg = c; return *this; }
App& App::panelWidth(float px) { p_->panelW = px; return *this; }
App& App::frameRate(double fps) {
    p_->targetFps = fps;
    p_->frameInterval = fps > 0.0 ? 1.0 / fps : 0.0;
    return *this;
}
App& App::maxDelta(double seconds) { p_->maxDeltaSeconds = seconds; return *this; }
App& App::idleThrottle(bool on) { p_->idleThrottleOn = on; return *this; }
App& App::busyWhen(std::function<bool()> fn) { p_->busyFn = fn; return *this; }
App& App::editorWidth(float px) { p_->editorW = px; return *this; }
App& App::editorEnabled(bool on) {
    p_->editorEnabled = on;
    if (!on) p_->editorOpen = false;
    return *this;
}
App& App::debugConsoleEnabled(bool on) {
    p_->debugConsoleEnabled = on;
    if (!on) p_->debugOpen = false;
    return *this;
}
App& App::editorFile(const std::string& path) {
    internal::setEditorFile(path);
    return *this;
}
App& App::editorOpen(bool on) { p_->editorOpen = on; return *this; }
bool App::editorOpen() const { return p_->editorOpen; }
bool App::exportProject(const std::string& outDir, const std::string& name) {
    internal::ExportOptions opt;
    opt.outDir = outDir;
    opt.name = name;
    internal::ExportReport r = internal::exportProject(opt);
    if (!r.error.empty()) EASEL_ERROR("%s", r.error.c_str());
    return r.ok;
}

App& App::onDraw(std::function<void(Canvas&)> fn) { p_->onDraw = std::move(fn); return *this; }
App& App::onPanel(std::function<void()> fn) { p_->onPanel = std::move(fn); return *this; }
App& App::onClick(std::function<void(Vec2, Mouse)> fn) { p_->onClick = std::move(fn); return *this; }
App& App::onDrag(std::function<void(const Drag&)> fn) { p_->onDrag = std::move(fn); return *this; }
App& App::onKey(std::function<void(Key)> fn) { p_->onKey = std::move(fn); return *this; }
App& App::onFrame(std::function<void(double)> fn) { p_->onFrame = std::move(fn); return *this; }
App& App::onStart(std::function<void()> fn) { p_->onStart = std::move(fn); return *this; }
App& App::onWindow(std::function<void(const Rect&)> fn) { p_->onWindow = std::move(fn); return *this; }
App& App::onExportCase(std::function<json()> fn) { p_->exportProvider = std::move(fn); return *this; }

App& App::welcome(const std::string& t, const std::string& d, std::function<void()> body) {
    p_->welTitle = t;
    p_->welDesc = d;
    p_->welBody = std::move(body);
    p_->welcomeRequested = true;
    p_->welcomeOpen = true;
    return *this;
}
void App::closeWelcome() { p_->welcomeOpen = false; }
bool App::welcomeOpen() const { return p_->welcomeOpen; }

App& App::transport(TimelineBase& tl) { p_->tl = &tl; return *this; }

void App::status(const std::string& s) { p_->statusText = s; }
void App::toast(const std::string& s) {
    p_->toastText = s;
    p_->toastUntil = ImGui::GetCurrentContext() ? ImGui::GetTime() + 2.5 : 0;
}
App& App::statusBar(bool on) { p_->statusBar = on; return *this; }

App& App::debugConsoleOpen(bool on) { p_->debugOpen = on && p_->debugConsoleEnabled; return *this; }
bool App::debugConsoleOpen() const { return p_->debugOpen; }

std::string App::exportDebugCase(const json& state, const std::string& note) {
    std::string path = debug::exportCase(state, note);
    if (!path.empty()) toast("已导出 " + path + "（命令行复现方法见日志）");
    return path;
}

std::string App::openPath() const { return cli::args().str("open"); }
bool        App::wantsSolve() const { return cli::args().has("solve"); }

Camera&     App::camera() { return p_->cam; }
Canvas&     App::canvas() { return p_->canvas; }
double      App::dpiScale() const { return p_->dpi; }
// Key 的数值就是 ImGuiKey（见 app.h 里 Key 的注释），转换零成本，直接强转丢给
// ImGui 查询就行。
bool        App::keyDown(Key k) const { return ImGui::IsKeyDown((ImGuiKey)k); }
// 帧号和运行时长都直接读 internal::Shared 里那两个字段（frame() 每帧开头更新的就是
// 它们），不另立一套计数器——这样 F12 调试台里「第 N 帧」、日志行首的 fN、和这里
// 读到的 frameCount() 永远是同一个数，对着日志排查时不会差一帧。
long long   App::frameCount() const { return shared().frame; }
double      App::elapsed() const { return shared().time; }
const char* App::backendName() const { return internal::backend::name(); }
void        App::quit() { p_->running = false; }

namespace internal {

namespace {

// "cmake version 3.31.6" -> "3.31.6"；ninja 的 --version 本来就是裸版本号，原样穿过。
std::string firstVersionNumber(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && !std::isdigit((unsigned char)line[i])) ++i;
    if (i >= line.size()) return line;
    size_t j = i;
    while (j < line.size() && (std::isdigit((unsigned char)line[j]) || line[j] == '.')) ++j;
    return line.substr(i, j - i);
}

// 起 <exe> --version，最多等 2 秒，只要第一行的版本号。拿不到就返回空——
// 调用方这时候只打路径，不瞎编版本号。
std::string quickVersion(const std::string& exe) {
    if (exe.empty()) return {};
    std::string out;
    int         code = -1;
    if (!runBlocking({exe, "--version"}, {}, 2000, &out, &code)) return {};
    size_t nl = out.find('\n');
    return firstVersionNumber(nl == std::string::npos ? out : out.substr(0, nl));
}

// 中文标签按显示宽度（CJK=2，其它=1）对齐到第 10 列，banner 那几行不会参差不齐。
int displayWidth(const std::string& s) {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        int len = (c & 0x80) == 0 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        w += len == 1 ? 1 : 2;
        i += (size_t)len;
    }
    return w;
}
std::string bannerLine(const std::string& label, const std::string& value) {
    std::string out = "  " + label;
    int         w = displayWidth(label);
    out.append((size_t)(w < 10 ? 10 - w : 1), ' ');   // 至少留一个空格，标签比 10 列还宽也不会粘住
    return out + value + "\n";
}

// <easelDir>/prebuilt/ 或（开发机本地）<easelDir>/build/default/prebuilt/：
// 哪个先探测到就是哪个，跟 src/new_project.cpp 生成的 CMakeLists 里 _easel_prebuilt_root
// 的判定顺序一模一样，别让工作台自己说的和实际配置工程时用的对不上。
std::string prebuiltRoot(const std::string& easelDir) {
    if (easelDir.empty()) return {};
    if (existsU8(joinPath(easelDir, "prebuilt/lib/cmake/easel/easelConfig.cmake")))
        return joinPath(easelDir, "prebuilt");
    if (existsU8(joinPath(easelDir, "build/default/prebuilt/lib/cmake/easel/easelConfig.cmake")))
        return joinPath(easelDir, "build/default/prebuilt");
    return {};
}

}  // namespace

// 版本那一行：banner() 的第一行，也是 `easel --version` 打的那一行。单独拎出来是因为
// EASEL_GIT_SHA / EASEL_ABI / EASEL_BUILT_WITH 这三个宏是 easel 这个 target 私有的
// （见根 CMakeLists.txt 的 target_compile_definitions），workbench/main.cpp 里看不见，
// 只能从库里要一份现成的字符串。
std::string versionLine() {
    std::string o = "Easel " EASEL_VERSION;
#if defined(EASEL_GIT_SHA)
    o += std::string(" · 提交 ") + EASEL_GIT_SHA;
#endif
#if defined(EASEL_ABI)
    o += std::string(" · abi ") + EASEL_ABI;
#endif
#if defined(EASEL_BUILT_WITH)
    o += std::string(" · ") + EASEL_BUILT_WITH;
#endif
    return o;
}

// 启动横幅：见 internal.h 的注释。GUI 启动、--doctor、每次 build/run/package/export
// 的日志开头都打它——出问题时先看这几行，不用去猜用的是哪个 Easel、哪份预编译。
std::string banner(const std::string& projectDir) {
    std::string o = versionLine() + "\n";

    o += bannerLine("程序", exePath());
    const EditorPaths& ep = editorPaths();
    o += bannerLine("Easel 源码", ep.easelDir.empty() ? "(没找到)" : ep.easelDir);

    std::string root = prebuiltRoot(ep.easelDir);
    if (ep.easelDir.empty()) {
        o += bannerLine("预编译", "(不知道 Easel 源码在哪，没法找)");
    } else if (root.empty()) {
        o += bannerLine("预编译", joinPath(ep.easelDir, "prebuilt") + "（没有预编译包，新建的工程会从源码编）");
    } else {
        json        pb = fs::loadJson(joinPath(root, "easel-prebuilt.json"));
        std::string pbAbi = pb.value("abi", std::string());
        std::string curAbi =
#if defined(EASEL_ABI)
            EASEL_ABI;
#else
            std::string("unknown");
#endif
        std::string status = pbAbi.empty() ? "读不出 abi，没法比对"
                             : (pbAbi == curAbi ? "匹配" : "不匹配（工程会走源码编）");
        o += bannerLine("预编译", root + "（abi " + (pbAbi.empty() ? "?" : pbAbi) + "，" + status + "）");
    }

    const Toolchain& tc = toolchain();
    o += bannerLine("编译器", tc.cxx.empty() ? "没找到 —— " + tc.note : tc.cxx + "（" + tc.source + "）");
    {
        std::string v = quickVersion(tc.cmake);
        o += bannerLine("cmake", tc.cmake.empty() ? "没找到 —— 装 CMake（brew install cmake / 工具箱里已自带）或把它加进 PATH"
                                                  : tc.cmake + (v.empty() ? "" : " " + v));
    }
    {
        std::string v = quickVersion(tc.ninja);
        o += bannerLine("ninja", tc.ninja.empty() ? "没找到（配置时会退回 Makefiles）" : tc.ninja + (v.empty() ? "" : " " + v));
    }
    // Linux 上少了图形开发包，配置时才会以一串 CMake 调用栈的形式爆出来（学生看不懂）。
    // 在这里提前报一句，缺什么直接给能照抄的安装命令。其它平台返回空串，这行不出现。
    {
        std::string gfx = internal::linuxGraphicsDevStatus();
        if (!gfx.empty()) o += bannerLine("图形开发包", gfx);
    }
    o += bannerLine("工程目录", projectDir.empty() ? "（没打开工程）" : projectDir);

    return o;
}

}  // namespace internal

std::string App::doctor() const {
    std::ostringstream o;
    o << internal::banner(internal::editorPaths().projectDir);
    o << doctorCore();
    o << "  ---- 图形 ----\n";
    o << "  渲染后端    : " << internal::backend::name() << "\n";
    o << "  显卡        : " << internal::backend::gpu() << "\n";
    o << "  DPI 缩放    : " << p_->dpi << "\n";
    o << "  窗口        : " << p_->w << " x " << p_->h << "\n";
    {
        std::string g = internal::linuxGraphicsDevStatus();
        if (!g.empty()) o << "  图形开发库  : " << g << "\n";
    }
    o << "  Dear ImGui  : " << IMGUI_VERSION << "   ImPlot: " << IMPLOT_VERSION << "\n";
    const internal::FontInfo& f = internal::fontInfo();
    o << "  ---- 字体 ----\n";
    o << "  来源        : " << f.source << "\n";
    o << "  文件        : " << (f.path.empty() ? "(内置)" : f.path) << "\n";
    o << "  中文字形    : " << (f.cjk ? "有" : "没有 —— 中文会显示成方框！") << "\n";
    o << "  等宽（代码） : " << (f.monoPath.empty() ? "(没找到)" : f.monoPath) << "\n";
    o << "  一列宽       : " << f.colRatio << " 字高\n";
    if (!f.note.empty()) o << "  提示        : " << f.note << "\n";
    o << "  ---- 编辑栏与导出 ----\n";
    const internal::EditorPaths& ep = internal::editorPaths();
    o << "  工程目录    : " << (ep.projectDir.empty() ? "(没打开工程)" : ep.projectDir) << "\n";
    o << "  Easel 源码  : " << (ep.easelDir.empty() ? "(不在旁边，导出时不带界面部分)" : ep.easelDir)
      << "\n";
    o << "  编辑的文件  : " << (ep.ok ? ep.file : "(没找到 src/solver.cpp)") << "\n";
    const internal::Toolchain& tc = internal::toolchain();
    o << "  编译器      : " << (tc.cxx.empty() ? "没找到 —— " + tc.note : tc.cxx) << "\n";
    if (!tc.cxx.empty()) {
        o << "  来源        : " << tc.source << "\n";
        std::string v = internal::toolchainVersion();
        if (!v.empty()) o << "  版本        : " << v << "\n";
    }
    o << "  ---- 声音 ----\n";
    o << "  音频设备    : " << internal::audioDoctor() << "\n";
    o << "  ---- 输出接管 ----\n";
    o << "  cout/printf : " << (internal::captureActive() ? "已接到日志窗"
                                                       : "未接管（--doctor 模式下本来就不接）")
      << "\n";
    if (internal::verbose()) {
        o << "  ---- 详细（--verbose）----\n";
        const char* pathEnv = std::getenv("PATH");
        o << "  PATH        : " << (pathEnv ? pathEnv : "(未设置)") << "\n";
        o << "  子进程 PATH 注入：" << internal::Proc::describe(tc.binDirs) << "\n";
        o << "  resolvePaths 候选（projectDir / easelDir）：\n";
        for (const std::string& l : internal::editorPathsDiag()) o << "    " << l << "\n";
        o << "  工具链探测（编译器 / cmake / ninja）：\n";
        for (const std::string& l : tc.diag) o << "    " << l << "\n";
        for (const char* rel : {"prebuilt/easel-prebuilt.json", "build/default/prebuilt/easel-prebuilt.json",
                                "VERSION.json"}) {
            if (ep.easelDir.empty()) break;
            std::string p = internal::joinPath(ep.easelDir, rel);
            std::string txt;
            if (internal::readTextU8(p, &txt)) o << "  " << p << "：\n" << txt << "\n";
        }
    }
    return o.str();
}

// ============================================================================
//  界面各部分
// ============================================================================
namespace {

// 状态灯：绿 = 一切正常，黄 = 有可疑的地方，红 = 出过错
enum class Light { Good, Warn, Bad };

Light computeLight(const Theme& th, std::string* why) {
    internal::Shared& s = shared();
    if (s.checkFailures > 0 || s.crashed) {
        *why = s.crashed ? "崩溃过一次" : ("EASEL_CHECK 失败 " + std::to_string(s.checkFailures) + " 次");
        return Light::Bad;
    }
    int warns = 0;
    for (const internal::LogEntry& e : s.log)
        if (e.level >= LogLevel::Warn) warns += e.repeat;
    if (warns > 0) {
        *why = "日志里有 " + std::to_string(warns) + " 条警告 / 错误";
        return Light::Warn;
    }
    if (!internal::fontInfo().cjk) {
        *why = "没有中文字体";
        return Light::Warn;
    }
    EASEL_UNUSED(th);
    *why = "一切正常";
    return Light::Good;
}

void drawTransport(App& app, TimelineBase* tl, const Rect& r, float dpi) {
    if (!tl) return;
    ImGui::SetNextWindowPos(ImVec2((float)r.x, (float)r.y));
    ImGui::SetNextWindowSize(ImVec2((float)r.w, (float)r.h));
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("##transport", nullptr, fl);
    int n = (int)tl->size();
    ImGui::BeginDisabled(n == 0);

    if (ImGui::Button(tl->playing() ? "暂停" : "播放", ImVec2(70 * dpi, 0))) tl->toggle();
    ImGui::SameLine();
    if (ImGui::Button("|<", ImVec2(34 * dpi, 0))) { tl->pause(); tl->rewind(); }
    ImGui::SameLine();
    if (ImGui::Button("<", ImVec2(34 * dpi, 0))) tl->step(-1);
    ImGui::SameLine();
    if (ImGui::Button(">", ImVec2(34 * dpi, 0))) tl->step(1);
    ImGui::SameLine();

    float speedW = 96 * dpi, labelW = 130 * dpi;
    ImGui::SetNextItemWidth(std::max(60.f * dpi,
                                     ImGui::GetContentRegionAvail().x - speedW - labelW - 24 * dpi));
    int idx = tl->index();
    if (ImGui::SliderInt("##frame", &idx, 0, n > 0 ? n - 1 : 0, "")) {
        tl->pause();
        tl->seek(idx);
    }
    ImGui::SameLine();
    ImGui::Text("第 %d / %d 帧", n ? tl->index() + 1 : 0, n);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(speedW);
    const char* speeds[] = {"0.25x", "0.5x", "1x", "2x", "4x", "8x"};
    const double vals[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
    int          cur = 2;
    for (int i = 0; i < 6; ++i)
        if (std::fabs(vals[i] - tl->speed()) < 1e-6) cur = i;
    if (ImGui::Combo("##speed", &cur, speeds, 6)) tl->speed(vals[cur]);

    ImGui::EndDisabled();
    ImGui::End();
    EASEL_UNUSED(app);
}

void drawStatusBar(App& app, const Theme& th, const Rect& r, float dpi,
                   const std::string& statusText, const Camera& cam) {
    ImGui::SetNextWindowPos(ImVec2((float)r.x, (float)r.y));
    ImGui::SetNextWindowSize(ImVec2((float)r.w, (float)r.h));
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, iv4(th.dark ? th.surface.darker(0.25f) : th.bg.darker(0.05f)));
    ImGui::Begin("##status", nullptr, fl);

    std::string why;
    Light       lt = computeLight(th, &why);
    Color       lc = lt == Light::Good ? th.good : (lt == Light::Warn ? th.warn : th.bad);
    ImVec2      p = ImGui::GetCursorScreenPos();
    float       rad = 5.f * dpi;
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(p.x + rad + 2 * dpi, p.y + ImGui::GetTextLineHeight() * 0.55f), rad, col(lc), 0);
    ImGui::Dummy(ImVec2(rad * 2 + 8 * dpi, 0));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", why.c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted(statusText.empty() ? why.c_str() : statusText.c_str());

    char right[64];
    std::snprintf(right, sizeof right, "缩放 %.2fx", cam.zoom());
    float rw = ImGui::CalcTextSize(right).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - rw - 16 * dpi);
    ImGui::TextUnformatted(right);

    ImGui::End();
    ImGui::PopStyleColor();
    EASEL_UNUSED(app);
}

void drawToast(const Theme& th, const std::string& text, double until, float dpi) {
    if (text.empty() || ImGui::GetTime() > until) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 24 * dpi),
                            ImGuiCond_Always, ImVec2(0.5f, 0.f));
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                          ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, iv4(th.accent.darker(0.35f)));
    ImGui::Begin("##toast", nullptr, fl);
    ImGui::TextUnformatted(text.c_str());
    ImGui::End();
    ImGui::PopStyleColor();
}

void drawScaleBar(Canvas& c, const Camera& cam, const Theme& th, float dpi) {
    double* mpu = cam.scaleBarUnit();
    if (!mpu) return;
    // 找一个「好看的」长度：1 / 2 / 5 × 10^n
    double targetPx = 140.0 * dpi;
    double worldLen = targetPx / cam.zoom();
    double meters = worldLen * (*mpu);
    double mag = std::pow(10.0, std::floor(std::log10(std::max(meters, 1e-9))));
    double nice = mag;
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (meters / mag >= m) nice = m * mag;
    double px = (nice / (*mpu)) * cam.zoom();

    Rect        vp = cam.viewport();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float       x0 = (float)(vp.x + 18 * dpi), y0 = (float)(vp.bottom() - 22 * dpi);
    ImU32       cc = col(th.fg.withAlpha(0.85f));
    dl->AddLine(ImVec2(x0, y0), ImVec2(x0 + (float)px, y0), cc, 2.f * dpi);
    dl->AddLine(ImVec2(x0, y0 - 5 * dpi), ImVec2(x0, y0 + 5 * dpi), cc, 2.f * dpi);
    dl->AddLine(ImVec2(x0 + (float)px, y0 - 5 * dpi), ImVec2(x0 + (float)px, y0 + 5 * dpi), cc, 2.f * dpi);
    char buf[64];
    if (nice >= 1000)
        std::snprintf(buf, sizeof buf, "%.1f 公里", nice / 1000.0);
    else
        std::snprintf(buf, sizeof buf, "%g %s", nice, cam.scaleBarName());
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.9f, ImVec2(x0, y0 - 22 * dpi), cc, buf);
    EASEL_UNUSED(c);
}

}  // namespace

// ============================================================================
//  主循环
// ============================================================================
int App::run() {
    Impl& d = *p_;
    bool  doctorOnly = cli::args().has("doctor");

    // --frames 0（以及负数）判非法，不再当成「不限制」。以前 0 走的是「不限制」那条路：
    // 程序静默地一直跑下去，还顺手把 --screenshot 吞了（永远到不了“最后一帧”，图就永远
    // 存不出来），而命令行上看不出任何异常。「不限制」是 --fps 0 的意思；同一个 0 在两个
    // 参数上表示两种意思太容易记混，所以这里直接拒绝，并把两条正路都说清楚。
    if (cli::args().has("frames") && cli::args().num("frames", 0) <= 0) {
        EASEL_ERROR("--frames 要一个 ≥ 1 的帧数，给的是 %lld。"
                    "想截当前这一帧：--frames 1 --screenshot x.png；想一直跑：不加 --frames。"
                    "（「不限制」是另一个参数：--fps 0 表示不限帧率。）",
                    cli::args().num("frames", 0));
        return 2;
    }

    // --export：不开窗口，导完就走。CI 拿它验「导出的工程真的能独立编译」。
    if (cli::args().has("export")) {
        std::string where = cli::args().str("export");
        if (where.rfind("--", 0) == 0) where.clear();
        internal::ExportOptions opt;
        opt.outDir = where;
        opt.name = cli::args().str("name");
        if (opt.name.rfind("--", 0) == 0) opt.name.clear();
        internal::ExportReport r = internal::exportProject(opt);
        std::printf("%s", r.checklist.c_str());
        if (!r.error.empty()) std::printf("导出失败：%s\n", r.error.c_str());
        std::fflush(stdout);
        return r.ok ? 0 : 1;
    }

    if (!internal::backend::init(d.title.c_str(), d.w, d.h, !doctorOnly, &d.win)) {
        if (doctorOnly) {
            // 自检工具在没有显卡的机器上更要能用（CI 的无头 runner 就是这样：没有 GPU，
            // glfwInit / 建窗口本来就会失败）——图形后端起不来时仍然把不依赖显卡的 core
            // 部分打出来，返回 0，而不是让 --doctor 本身也“挂掉”。
            std::string reason;
            for (auto it = internal::shared().log.rbegin(); it != internal::shared().log.rend(); ++it) {
                if (it->level == LogLevel::Error) { reason = it->msg; break; }
            }
            std::ostringstream o;
            o << internal::banner(internal::editorPaths().projectDir);
            o << doctorCore();
            o << "  ---- 图形 ----\n";
            o << "  图形后端起不来：" << (reason.empty() ? "原因不明（backend::init 返回 false）" : reason)
              << "\n";
            {
                std::string g = internal::linuxGraphicsDevStatus();
                if (!g.empty()) o << "  图形开发库  : " << g << "\n";
            }
            std::printf("%s", o.str().c_str());
            std::fflush(stdout);
            return 0;
        }
        EASEL_ERROR("Easel 起不来。用 --doctor 看看环境。");
        return 1;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // 不要在学生的工程目录里拉屎
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    d.dpi = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    if (d.dpi <= 0.f) d.dpi = 1.f;
    internal::applyTheme(d.theme);
    ImGui::GetStyle().ScaleAllSizes(d.dpi);
    ImGui::GetStyle().FontScaleDpi = d.dpi;
    internal::buildFonts(d.theme, d.dpi);

    if (doctorOnly) {
        internal::backend::newFrame();   // 让字体真正建出来，IsGlyphInFont 才准
        ImGui::EndFrame();
        std::string report = doctor();
        internal::backend::shutdown();
        std::printf("%s", report.c_str());
        std::fflush(stdout);
        return 0;
    }

    internal::installHooks(true);   // 现在窗口有了，CHECK 可以弹红条而不是崩
    internal::startCapture();
    if (cli::args().has("debug") && d.debugConsoleEnabled) d.debugOpen = true;   // app --debug 直接把调试台打开
    // --edit / --run 只对开着编辑栏的程序有意义；工具类程序（工作台）把编辑栏关了，
    // 它自己的 --run 是另一个意思，别在这里抢走。
    if (d.editorEnabled) {
        if (cli::args().has("edit")) d.editorOpen = true;
        // app --run：开起来就编译并运行一次。CI 拿它验整条「编辑栏 → g++ → 输出」的链子。
        if (cli::args().has("edit-run")) { d.editorOpen = true; d.runOnce = true; }
    }
    if (d.onStart) d.onStart();                        // 显卡准备好了，可以 loadTexture 了
    EASEL_LOG("Easel %s 启动（%s，DPI %.2f，字体 %s）", EASEL_VERSION, internal::backend::name(),
              (double)d.dpi, internal::fontInfo().path.empty() ? "内置" : internal::fontInfo().path.c_str());

    // --warmup N：截图/录屏经常错过"要等几秒才发生"的效果。真正进入下面那个主循环之前，
    // 先只调 N 次 onFrame(dt)——不渲染、不 present、不摸窗口系统——dt 固定用 1/60，
    // 不跟真实时钟走，同一条命令永远预热出同一个状态，配合 --frames/--screenshot 才稳。
    long long warmup = cli::args().has("warmup") ? cli::args().num("warmup", 0) : 0;
    if (warmup > 0) {
        EASEL_LOG("--warmup %lld：只跑逻辑不渲染，预热 %lld 帧（dt 固定 1/60）", warmup, warmup);
        for (long long i = 0; i < warmup; ++i) {
            // 帧号照样往前走：预热的就是「帧」，靠 app.frameCount() 驱动动画的作品必须
            // 也能被 --warmup 快进，否则这个参数对它们等于没有。运行时长 elapsed() 不
            // 跟着动——它是真实时钟，预热是一瞬间跑完的，硬凑一个假时间反而会在进主
            // 循环时跳回去。
            shared().frame++;
            if (d.onFrame) d.onFrame(1.0 / 60.0);
        }
    }

    d.lastTime = glfwGetTime();
    d.lastActiveTime = d.lastTime;
    d.clear = d.customBg ? d.bg : d.theme.bg;
    d.io = &io;

    // 给 CI / 截图用的：跑 N 帧就退出，顺手存张图
    d.maxFrames = cli::args().has("frames") ? cli::args().num("frames", 0) : -1;
    d.shotPath = cli::args().str("screenshot");
    d.framesRun = 0;

    // --fps N 在 frameRate() 之后覆盖：命令行的意愿比代码里写死的默认值优先级更高
    // （比如在没有显卡驱动的机器上临时把帧率压到个位数验证画面还对不对）。
    if (cli::args().has("fps")) frameRate((double)cli::args().num("fps", 60));

    // 网页版（Emscripten）只需要把这个 while 换成 emscripten_set_main_loop(frame)，
    // 所以循环体不许再长回 run() 里 —— 都在 frame() 里；帧率上限的 sleep 只在这个
    // 桌面循环里做（浏览器本来就靠 requestAnimationFrame 控速，不需要我们自己补）。
    while (d.running && !glfwWindowShouldClose(d.win)) {
        auto t0 = std::chrono::steady_clock::now();
        frame();
        // 垂直同步失效时的兜底：按 frame() 算出来的目标间隔（正常是 1/frameRate()，
        // 最小化 / 闲时会被临时放大到 100ms）补 sleep。sleep 前留 1ms 余量，避免因为
        // sleep_for 本身的调度误差睡过头、把下一帧顶到间隔之外。
        if (d.frameInterval > 0.0) {
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            double remain = d.frameInterval - elapsed - 0.001;
            if (remain > 0.0) std::this_thread::sleep_for(std::chrono::duration<double>(remain));
        }
    }

    EASEL_LOG("退出");
    internal::audioShutdown();   // 关声卡（没开过就什么都不做）
    internal::stopCapture();
    internal::removeHooks();
    internal::backend::shutdown();
    return 0;
}

void App::frame() {
    Impl&    d = *p_;
    ImGuiIO& io = *d.io;

    glfwPollEvents();
    internal::pumpCapture();
    internal::editorPump();   // 编辑栏起的编译 / 运行进程，有多少输出收多少
    if (d.runOnce && d.framesRun >= 2) { d.runOnce = false; internal::editorRun(); }

    double now = glfwGetTime();
    // dt 上限默认 0.05s（20fps 一帧）：窗口被挡一下再回来，现实里过去的时间可能是
    // 好几秒，不夹住的话粒子/物理模拟会一帧瞬移到很远的地方。maxDelta(0) = 不夹。
    double rawDt = now - d.lastTime;
    double dt = d.maxDeltaSeconds > 0.0 ? clamp(rawDt, 0.0, d.maxDeltaSeconds) : std::max(rawDt, 0.0);
    d.lastTime = now;
    shared().frame++;
    shared().time = now;

    // ---------------- 最小化 / 窗口不可见：只维持状态，不画 ----------------
    // iconified，或者 framebuffer 宽高是 0（比如切到另一个虚拟桌面）时，newFrame /
    // 画布 / swap 全部没必要做，省下一整块 GPU + CPU。dt 照样喂给 onFrame，动画状态
    // （计时器之类）不会因为最小化过一段时间突然跳变。
    // 注意：这个 return 发生在 d.framesRun++ 和 --screenshot 判断之前，所以
    // --frames N / --screenshot 在这条路径下不计数、不触发——不然无头 CI 如果窗口
    // 一开始就以最小化状态创建，会在这里被拦住永远走不到画面那条路径；sleep 时长
    // 写死 100ms，不跟着 frameRate() 走（哪怕 frameRate(0) 不限帧，最小化时也不该空转）。
    bool hidden = glfwGetWindowAttrib(d.win, GLFW_ICONIFIED) != 0;
    if (!hidden) {
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(d.win, &fbw, &fbh);
        hidden = (fbw <= 0 || fbh <= 0);
    }
    if (hidden) {
        if (d.onFrame) d.onFrame(dt);
        d.frameInterval = 0.1;
        std::this_thread::sleep_for(std::chrono::duration<double>(0.1));
        return;
    }

    if (d.themeDirty) {
        d.themeDirty = false;
        internal::applyTheme(d.theme);
        ImGui::GetStyle().ScaleAllSizes(d.dpi);
        ImGui::GetStyle().FontScaleDpi = d.dpi;
        internal::buildFonts(d.theme, d.dpi);
        d.clear = d.customBg ? d.bg : d.theme.bg;
    }

    internal::backend::newFrame();
    glfwGetWindowSize(d.win, &d.w, &d.h);

    internal::Shared& sh = shared();
    sh.hasOnDraw = (bool)d.onDraw;
    sh.hasOnPanel = (bool)d.onPanel;
    sh.exportProvider = d.exportProvider ? &d.exportProvider : nullptr;
    sh.canvas = &d.canvas;
    sh.camera = &d.cam;
    sh.timeline = d.tl;
    sh.dpi = d.dpi;

    if (d.onFrame) d.onFrame(dt);
    if (d.tl) d.tl->update(dt);

    // ---------------- 布局 ----------------
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float W = vp->WorkSize.x, H = vp->WorkSize.y;
    // 状态栏默认不显示（statusBar(false)）；F12 调试台打开时临时出现，方便看状态灯 / 缩放。
    float statusH = (d.statusBar || d.debugOpen) ? (ImGui::GetTextLineHeightWithSpacing() + 14 * d.dpi) : 0.f;
    float transH = d.tl ? (ImGui::GetFrameHeight() + 20 * d.dpi) : 0.f;
    float panelW = d.onPanel ? clamp(d.panelW, 200 * d.dpi, W * 0.6) : 0.f;
    // 编辑栏在最左边，画布让位（D-27 第 2 条：不做成浮动窗口，长时间打字要靠着边）
    float editorW = d.editorOpen
                        ? (float)clamp(d.editorW * d.dpi, 320 * d.dpi,
                                        std::max(360.0 * d.dpi, (double)W - panelW - 160 * d.dpi))
                        : 0.f;

    d.canvasRect = Rect(vp->WorkPos.x + editorW, vp->WorkPos.y, W - panelW - editorW,
                        H - statusH - transH);
    Rect transRect(vp->WorkPos.x + editorW, vp->WorkPos.y + d.canvasRect.h, W - panelW - editorW,
                   transH);
    Rect statusRect(vp->WorkPos.x, vp->WorkPos.y + H - statusH, W, statusH);
    Rect panelRect(vp->WorkPos.x + W - panelW, vp->WorkPos.y, panelW, H - statusH);
    Rect editorRect(vp->WorkPos.x, vp->WorkPos.y, editorW, H - statusH);

    // ---------------- 画布 ----------------
    d.cam.setViewport(d.canvasRect);
    bool canvasHovered = d.canvasRect.contains(internal::ev(io.MousePos)) && !io.WantCaptureMouse;
    d.cam.handleInput(canvasHovered);

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(iv(d.canvasRect.min()), iv(d.canvasRect.max()), col(d.clear));
    bg->PushClipRect(iv(d.canvasRect.min()), iv(d.canvasRect.max()), true);
    d.canvas.begin(d.cam, bg, canvasHovered, d.dpi);
    sh.canvasRecording = true;   // Graphics::begin() 靠这个拒绝"在 onDraw 里离屏画布 begin()"
    if (d.onDraw) d.onDraw(d.canvas);
    sh.canvasRecording = false;
    drawScaleBar(d.canvas, d.cam, d.theme, d.dpi);
    d.canvas.end();
    bg->PopClipRect();

    // ---------------- 占满画布的自定义界面（工具类程序）----------------
    if (d.onWindow) d.onWindow(d.canvasRect);

    // ---------------- 画布上的鼠标 ----------------
    auto worldNow = [&] { return d.cam.toWorld(internal::ev(io.MousePos)); };
    for (int b = 0; b < 2; ++b) {
        Mouse btn = b == 0 ? Mouse::Left : Mouse::Right;
        int   imb = b == 0 ? ImGuiMouseButton_Left : ImGuiMouseButton_Right;
        bool  spacePan = ImGui::IsKeyDown(ImGuiKey_Space);
        if (canvasHovered && ImGui::IsMouseClicked(imb) && !spacePan) {
            d.pressed = true;
            d.pressBtn = btn;
            d.pressScreen = internal::ev(io.MousePos);
            d.drag = Drag{};
            d.drag.start = worldNow();
            d.drag.current = d.drag.start;
            d.drag.button = btn;
            d.dragVelN = 0;
        }
        if (d.pressed && d.pressBtn == btn && ImGui::IsMouseDragging(imb, 3.0f)) {
            Vec2 w = worldNow();
            d.drag.delta = w - d.drag.current;
            d.drag.current = w;
            d.drag.began = !d.dragging;
            d.drag.ended = false;
            d.dragging = true;
            // velocity：这一帧的瞬时速度 delta/dt，进最近 3 帧的历史再取平均——
            // 松手前恰好有一帧停顿（dt 很小或 delta 是 0）不会把「甩出去」的速度拖没。
            // dt<=0（极少见，比如同一时钟刻两次 PollEvents）就用上一次算出来的瞬时值。
            Vec2 instVel = dt > 1e-9 ? d.drag.delta * (1.0 / dt)
                                     : (d.dragVelN > 0 ? d.dragVelHist[(d.dragVelN - 1) % 3] : Vec2{});
            d.dragVelHist[d.dragVelN % 3] = instVel;
            ++d.dragVelN;
            int  histN = d.dragVelN < 3 ? d.dragVelN : 3;
            Vec2 avg;
            for (int k = 0; k < histN; ++k) avg += d.dragVelHist[k];
            d.drag.velocity = avg / (double)histN;
            if (d.onDrag) d.onDrag(d.drag);
            d.drag.began = false;
        }
        if (d.pressed && d.pressBtn == btn && ImGui::IsMouseReleased(imb)) {
            if (d.dragging) {
                d.drag.delta = {0, 0};
                d.drag.current = worldNow();
                d.drag.ended = true;
                // 注意：velocity 不在这里重算——它保留的是上面那个分支里松手前最后
                // 一次算出来的平滑值，"甩出去"要用的就是这个。
                if (d.onDrag) d.onDrag(d.drag);
            } else if (dist(d.pressScreen, internal::ev(io.MousePos)) < 4.0 * d.dpi) {
                if (d.onClick) d.onClick(worldNow(), btn);
            }
            d.pressed = false;
            d.dragging = false;
        }
    }

    // ---------------- 键盘 ----------------
    if (d.debugConsoleEnabled && ImGui::IsKeyPressed(ImGuiKey_F12, false)) d.debugOpen = !d.debugOpen;
    // F9 / F5 要在输入框里也管用，所以放在 WantCaptureKeyboard 判断之前
    if (d.editorEnabled) {
        if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) d.editorOpen = !d.editorOpen;
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
            d.editorOpen = true;
            internal::editorRun();
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && d.welcomeOpen) d.welcomeOpen = false;
    if (d.onKey && !io.WantCaptureKeyboard) {
        // 上界是 ImGuiKey_MouseLeft，不是 ImGuiKey_NamedKey_END：ImGui 1.89 起，鼠标键
        // （ImGuiKey_MouseLeft/Right/Middle/X1/X2 和两个滚轮轴）以及内部保留的修饰键
        // 存储位（ImGuiKey_ReservedForMod*）都排在具名按键区间的末尾，一路 IsKeyPressed
        // 扫过去会把「点一下画布」也派发成一次按键 —— 学生写「按任意键重开」，鼠标
        // 一点就重开了。Easel 的鼠标另有一套模型（onClick(Vec2, Mouse) + enum class
        // Mouse），两套不能混着用，所以 onKey 只派发真正的键盘键，到鼠标别名为止。
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_MouseLeft; ++k)
            if (ImGui::IsKeyPressed((ImGuiKey)k, false)) d.onKey((Key)k);
    }

    // ---------------- 面板 ----------------
    if (d.onPanel) {
        ImGui::SetNextWindowPos(iv(panelRect.min()));
        ImGui::SetNextWindowSize(iv({panelRect.w, panelRect.h}));
        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("##panel", nullptr, fl);
        ui::title(d.title.c_str());
        ImGui::Separator();
        ImGui::Spacing();
        d.onPanel();
        ImGui::End();

        // 面板左边缘的分隔条，可以拖
        ImGui::SetNextWindowPos(ImVec2((float)panelRect.x - 4 * d.dpi, (float)panelRect.y));
        ImGui::SetNextWindowSize(ImVec2(8 * d.dpi, (float)panelRect.h));
        ImGui::SetNextWindowBgAlpha(0.f);
        ImGui::Begin("##splitter", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
        ImGui::InvisibleButton("##grip", ImVec2(8 * d.dpi, (float)panelRect.h));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) d.panelW -= io.MouseDelta.x;
        ImGui::End();
    }

    // ---------------- 编辑栏 ----------------
    if (d.editorOpen) {
        internal::drawEditor(*this, editorRect, &d.editorOpen);

        ImGui::SetNextWindowPos(ImVec2((float)editorRect.w - 4 * d.dpi, (float)editorRect.y));
        ImGui::SetNextWindowSize(ImVec2(8 * d.dpi, (float)editorRect.h));
        ImGui::SetNextWindowBgAlpha(0.f);
        ImGui::Begin("##editorsplit", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
        ImGui::InvisibleButton("##egrip", ImVec2(8 * d.dpi, (float)editorRect.h));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) d.editorW += io.MouseDelta.x / d.dpi;
        ImGui::End();
    }

    // ---------------- 播放条、状态栏、横幅、调试台 ----------------
    drawTransport(*this, d.tl, transRect, d.dpi);
    if (statusH > 0.f) drawStatusBar(*this, d.theme, statusRect, d.dpi, d.statusText, d.cam);
    internal::drawCheckBanner(*this);
    drawToast(d.theme, d.toastText, d.toastUntil, d.dpi);
    if (d.debugOpen) internal::drawDebugConsole(&d.debugOpen, *this);
    if (d.showImGuiDemo) ImGui::ShowDemoWindow(&d.showImGuiDemo);

    // ---------------- 启动页 ----------------
    if (d.welcomeOpen) {
        ImGui::OpenPopup("##welcome");
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + W * 0.5f, vp->WorkPos.y + H * 0.42f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(clamp(W * 0.55, 420 * d.dpi, 640 * d.dpi), 0));
        if (ImGui::BeginPopupModal("##welcome", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);
            ImGui::TextUnformatted(d.welTitle.c_str());
            ImGui::PopFont();
            ImGui::Spacing();
            ui::help(d.welDesc.c_str());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            if (d.welBody) d.welBody();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, iv4(d.theme.muted));
            ImGui::TextUnformatted("界面基础库 Easel " EASEL_VERSION " · 代码酷 daimaku.net");
            ImGui::PopStyleColor();
            if (!d.welcomeOpen) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // ---------------- 工具类程序闲时降频 ----------------
    // idleThrottle(true) 时：这一帧要是没有任何输入，且已经闲了超过 0.5 秒，就把
    // 目标间隔放大到 100ms（10 帧）；一有输入、编辑栏子进程在跑、或者 toast / 横幅
    // 正显示着，立刻恢复满速（frameRate() 设的那个间隔）。判断输入用 io.MousePos
    // 有没有变（io.MouseDelta 就是这个）、滚轮、任意鼠标键、字符输入、任意键盘键——
    // 比只看 WantCaptureMouse/WantCaptureKeyboard 更稳妥，那两个只说「这次输入 ImGui
    // 要不要」，跟「用户是不是刚动过」是两回事。
    if (d.idleThrottleOn) {
        bool active = (io.MouseDelta.x != 0.f || io.MouseDelta.y != 0.f) ||
                      io.MouseWheel != 0.f || io.MouseWheelH != 0.f ||
                      ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                      ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                      ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                      io.InputQueueCharacters.Size > 0 || io.WantTextInput;
        if (!active) {
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                if (ImGui::IsKeyDown((ImGuiKey)k)) { active = true; break; }
            }
        }
        bool busy = active || internal::editorBusy() || sh.banner ||
                    (!d.toastText.empty() && now <= d.toastUntil) ||
                    (d.busyFn && d.busyFn());
        if (busy) d.lastActiveTime = now;
        bool idle = (now - d.lastActiveTime) > 0.5;
        d.frameInterval = idle ? 0.1 : (d.targetFps > 0.0 ? 1.0 / d.targetFps : 0.0);
    } else {
        d.frameInterval = d.targetFps > 0.0 ? 1.0 / d.targetFps : 0.0;
    }

    // --frames N 的最后一帧：截图要在「画完、还没交换缓冲区」的那一刻拍。
    // 以前是 present() 之后才 screenshot()，而 present() 最后一步是 glfwSwapBuffers()，
    // readPixels() 读的 GL_BACK 那时已经不是刚画好的这一帧了（第一帧时更是一块没画过的
    // 空缓冲）—— `--frames 1 --screenshot x.png` 存出纯色空图、还报成功就是这么来的。
    // 所以「这是不是最后一帧」提前算好，截图动作交给 present() 在交换之前回调。
    bool                  lastFrame = d.maxFrames > 0 && d.framesRun + 1 >= d.maxFrames;
    std::function<void()> shot;
    if (lastFrame && !d.shotPath.empty()) shot = [&] { screenshot(d.shotPath); };
    internal::backend::present(d.win, d.clear, shot);

    ++d.framesRun;
    if (lastFrame) d.running = false;
}

}  // namespace easel
