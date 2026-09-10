// Easel — app.cpp  窗口、主循环、布局、状态灯
#include "internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>

#include <chrono>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <cstdio>
#endif

namespace easel {

#if defined(_WIN32)
// GUI 子系统的程序没有控制台；从命令行跑 --doctor / --export 时把输出接回父终端。
// 双击启动时没有父控制台，AttachConsole 失败，什么都不做（也不弹黑框）。
static void attachParentConsole() {
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
    std::function<void(int)>         onKey;
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

    Rect canvasRect{0, 0, 1, 1};
    bool themeDirty = false;
};

// ---------------------------------------------------------------- 构造
App::App(int argc, char** argv) : p_(new Impl) {
#if defined(_WIN32)
    attachParentConsole();
#endif
    g_instance = this;
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
App& App::onKey(std::function<void(int)> fn) { p_->onKey = std::move(fn); return *this; }
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
const char* App::backendName() const { return internal::backend::name(); }
void        App::quit() { p_->running = false; }

std::string App::doctor() const {
    std::ostringstream o;
    o << doctorCore();
#if defined(EASEL_GIT_SHA) && defined(EASEL_BUILT_WITH)
    o << "Easel " << EASEL_VERSION << " · 提交 " << EASEL_GIT_SHA
#if defined(EASEL_ABI)
      << " · abi " << EASEL_ABI
#endif
      << " · " << EASEL_BUILT_WITH << "\n";
#endif
    o << "  ---- 图形 ----\n";
    o << "  渲染后端    : " << internal::backend::name() << "\n";
    o << "  显卡        : " << internal::backend::gpu() << "\n";
    o << "  DPI 缩放    : " << p_->dpi << "\n";
    o << "  窗口        : " << p_->w << " x " << p_->h << "\n";
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
    double dt = clamp(now - d.lastTime, 0.0, 0.25);
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
    if (d.onDraw) d.onDraw(d.canvas);
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
        }
        if (d.pressed && d.pressBtn == btn && ImGui::IsMouseDragging(imb, 3.0f)) {
            Vec2 w = worldNow();
            d.drag.delta = w - d.drag.current;
            d.drag.current = w;
            d.drag.began = !d.dragging;
            d.drag.ended = false;
            d.dragging = true;
            if (d.onDrag) d.onDrag(d.drag);
            d.drag.began = false;
        }
        if (d.pressed && d.pressBtn == btn && ImGui::IsMouseReleased(imb)) {
            if (d.dragging) {
                d.drag.delta = {0, 0};
                d.drag.current = worldNow();
                d.drag.ended = true;
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
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
            if (ImGui::IsKeyPressed((ImGuiKey)k, false)) d.onKey(k);
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

    internal::backend::present(d.win, d.clear);

    ++d.framesRun;
    if (d.maxFrames > 0 && d.framesRun >= d.maxFrames) {
        if (!d.shotPath.empty()) screenshot(d.shotPath);
        d.running = false;
    }
}

}  // namespace easel
