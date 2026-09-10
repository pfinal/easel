// Easel — internal.h  库内部共享的东西。学生看不到这个文件。
#ifndef EASEL_INTERNAL_H
#define EASEL_INTERNAL_H

#include <easel/easel.h>

#include <imgui.h>
#include <implot.h>

#include <deque>
#include <map>

struct GLFWwindow;

namespace easel {
namespace internal {

// ---------------------------------------------------------------- 类型转换
inline ImU32 col(const Color& c) {
    return IM_COL32((int)(clamp(c.r, 0, 1) * 255 + 0.5), (int)(clamp(c.g, 0, 1) * 255 + 0.5),
                    (int)(clamp(c.b, 0, 1) * 255 + 0.5), (int)(clamp(c.a, 0, 1) * 255 + 0.5));
}
inline ImVec2 iv(const Vec2& v) { return ImVec2((float)v.x, (float)v.y); }
inline ImVec4 iv4(const Color& c) { return ImVec4(c.r, c.g, c.b, c.a); }
inline Vec2   ev(const ImVec2& v) { return {(double)v.x, (double)v.y}; }

// ---------------------------------------------------------------- 主题
void applyTheme(const Theme& t);

// ---------------------------------------------------------------- 后端
namespace backend {
bool        init(const char* title, int w, int h, bool visible, GLFWwindow** outWindow);
void        newFrame();
void        present(GLFWwindow* win, const Color& clear);
void        shutdown();
const char* name();
std::string gpu();
// pixelated=true：采样用 NEAREST（硬边，适合像素风精灵/马赛克）；false：线性（平滑）。
bool        createTexture(const unsigned char* rgba, int w, int h, std::uint64_t* outId,
                          bool pixelated = false);
void        destroyTexture(std::uint64_t id);
bool        readPixels(int* w, int* h, std::vector<unsigned char>* rgba);

// ---------------------------------------------------------------- 离屏渲染目标（Graphics）
// GL：真正的 FBO + 颜色贴图。DX11：全部空实现 / 返回 false —— 已知缺口（见 backend.cpp
// DX11 分支的注释），学生在 DX11 后端下改用 Layer。
// createRenderTarget 失败时不用自己 EASEL_WARN——调用方（Graphics::create）统一报。
bool createRenderTarget(int w, int h, std::uint64_t* outTexId, std::uint64_t* outFboId);
void destroyRenderTarget(std::uint64_t texId, std::uint64_t fboId);
// 绑 FBO、把 viewport 设成整块缓冲、把当前 FBO/viewport 存起来（endRenderTarget 还原用）。
bool beginRenderTarget(std::uint64_t fboId, int w, int h);
void endRenderTarget();
void clearRenderTarget(const Color& c);
// 把一个 ImDrawList 画进当前绑定的渲染目标（beginRenderTarget 已经绑好）。
// dl 的静态类型是 ImDrawList*，写成 void* 是为了不让 internal.h 之外、只 #include
// <easel/canvas.h> 的代码也被迫看见 imgui.h（internal.h 本身已经 include 了 imgui.h，
// 这里其实可以直接写 ImDrawList*，但保持和 Canvas::begin(void* drawList) 同样的风格）。
bool renderDrawList(void* dl, int w, int h);
}  // namespace backend

// ---------------------------------------------------------------- 字体
struct FontInfo {
    std::string path;        // 实际用的字体文件
    std::string source;      // "主题指定" / "环境变量 EASEL_FONT" / "内置" / "系统" / "ImGui 默认"
    float       sizePx = 17.f;
    bool        cjk = false; // 有没有中文字形
    std::string monoPath;    // 编辑栏的等宽字体
    float       colRatio = 0.f;  // 一列有多宽（相对字高）—— 编辑栏的中文就缩到这么大
    std::string note;
};
void            buildFonts(const Theme& theme, float dpiScale);
const FontInfo& fontInfo();
ImFont*         monoFont();
ImFont*         codeFont();   // 编辑栏专用：中文按列宽缩小合并（见 font.cpp 的注释）

// ---------------------------------------------------------------- 日志与追踪
struct LogEntry {
    LogLevel    level = LogLevel::Info;
    std::string file;
    int         line = 0;
    std::string msg;
    long long   frame = 0;
    int         repeat = 1;
};

struct TraceSeries {
    std::string         name;
    std::vector<double> xs, ys;
};

struct DbgValue {
    std::string key, value;
    long long   frame = 0;
    std::vector<double> history;   // 数值型的话留一段迷你折线
    bool                numeric = false;
};

// 库内部的全局状态。App 只有一个，所以放一个单例最省事。
struct Shared {
    // 日志
    std::deque<LogEntry> log;
    size_t               logLimit = 4000;
    bool                 logAutoScroll = true;
    int                  logLevelFilter = 0;
    char                 logFilter[128] = {0};
    int                  droppedLogs = 0;

    // EASEL_TRACE
    std::vector<TraceSeries> traces;
    size_t                   traceLimit = 20000;

    // easel::dbg
    std::vector<DbgValue> dbgs;

    // EASEL_CHECK 失败横幅
    bool        banner = false;
    std::string bannerExpr, bannerMsg, bannerWhere;
    int         checkFailures = 0;

    // 崩溃遗留
    bool        crashed = false;
    std::string crashWhat, crashStack;

    long long frame = 0;
    double    time = 0;

    // 每帧由 App 填好，调试台读它（省得到处传 App&）
    bool                   hasOnDraw = false;
    bool                   hasOnPanel = false;
    std::function<json()>* exportProvider = nullptr;
    Canvas*                canvas = nullptr;
    Camera*                camera = nullptr;
    TimelineBase*          timeline = nullptr;
    float                  dpi = 1.f;
    // 主画布正在录制（app.cpp 的 frame() 在 d.canvas.begin()/end() 之间置真）。
    // Graphics::begin() 用它判断"是不是在 onDraw 里被调用"——那时候主画布正忙，
    // Graphics 自己的 begin/end 不该在这时候插进来。
    bool                   canvasRecording = false;

    void addLog(LogLevel lv, const char* file, int line, const std::string& msg);
    void addTrace(const char* name, double value, long long index);
    void addDbg(const char* key, const char* value);
    void clearFrameDbg();
};

Shared& shared();

// stdout 接管：cout / printf 同时进终端和日志窗（D-23 第 3 条）
void startCapture();
void pumpCapture();     // 每帧调
void stopCapture();
void writeThrough(const char* s, size_t n);   // 写到真正的 stdout（不再被抓）
bool captureActive();

// 把钩子接到界面上 / 摘掉。
// withCheck=false 时 EASEL_CHECK 仍然走命令行那套（打印 + 停住）——
// App 还没跑起来的时候，红色横幅没人看得见，不如老老实实 abort。
void installHooks(bool withCheck);
void removeHooks();

// ---------------------------------------------------------------- 调试台
void drawDebugConsole(bool* open, App& app);
void drawCheckBanner(App& app);

// ---------------------------------------------------------------- 文件（中文路径安全）
// core.h 里的 fs:: 用的是窄字符 API，Windows 上遇到中文目录会打不开文件。
// 库内部一律走这几个（学生那边不受影响：他们的路径由 CMake 传，或者是 ASCII）。
#if defined(_WIN32)
std::wstring widen(const std::string& s);
std::string  narrow(const std::wstring& w);
#endif
std::FILE* fopenU8(const std::string& path, const char* mode);
bool       readTextU8(const std::string& path, std::string* out);
bool       writeTextU8(const std::string& path, const std::string& text);
bool       fileStamp(const std::string& path, long long* mtime, long long* size);
bool       existsU8(const std::string& path);
bool       isDirU8(const std::string& path);
bool       makeDirsU8(const std::string& path);
bool       copyFileU8(const std::string& from, const std::string& to);
struct DirEntry {
    std::string name;
    bool        isDir = false;
    long long   size = 0;
};
std::vector<DirEntry> listDirU8(const std::string& dir);
// 整棵目录拷过去（跳过 build/ .git/ 之类的构建垃圾）
bool copyTreeU8(const std::string& from, const std::string& to, int* files = nullptr,
                long long* bytes = nullptr);
// 整棵目录删掉（递归）。不存在也算成功——测试的临时目录清理专用。
bool removeTreeU8(const std::string& path);

// ---------------------------------------------------------------- 声音（D-33）
// 设备是懒开的：第一次调 audio:: 才开。App 退出前调一次 audioShutdown() 关掉。
void        audioShutdown();
std::string audioDoctor();     // App::doctor() 里的那一行（会顺便真的去开一次设备）
// 下面这几个是 audio.cpp 里的纯函数，测试直接调，一个都不碰声卡。
void audioFFT(float* re, float* im, int n);            // 原地 radix-2，n 必须是 2 的幂
void audioBandBins(int bands, int fftN, double sampleRate, int* lo, int* hi);  // 对数分频段
void audioAnalyze(const float* mono, int n, int bands, double sampleRate, float* out);
// 环形缓冲区（平时只有音频线程往里写）——测试用它假装有声音在放
void audioFeedForTest(const float* frames, int count, int channels);
int  audioReadRecent(float* out, int n);
void audioClearRing();

// ---------------------------------------------------------------- 子进程
// 非阻塞：start() 之后每帧 pump() 一次。不开线程（D-11），界面不会卡。
class Proc {
public:
    Proc();
    ~Proc();
    Proc(const Proc&) = delete;
    Proc& operator=(const Proc&) = delete;

    // pathPrepend：临时插到子进程 PATH 最前面的目录（cmake 要能找到 g++ / ninja）
    bool start(const std::vector<std::string>& argv, const std::string& workDir,
               std::string* error = nullptr,
               const std::vector<std::string>* pathPrepend = nullptr);
    bool pump(std::string* appended);       // 返回 true = 还活着
    void kill();
    bool running() const { return running_; }
    int  exitCode() const { return exitCode_; }
    const std::string& cmdline() const { return cmd_; }

    static std::string describe(const std::vector<std::string>& argv);
    static const long long kMaxOutput = 8LL << 20;   // 8 MB 封顶，防死循环刷屏

    struct Impl;

private:
    void        reap();
    Impl*       p_;
    bool        running_ = false;
    int         exitCode_ = -1;
    long long   bytes_ = 0;
    std::string cmd_;
};

// ---------------------------------------------------------------- 工具链
struct Toolchain {
    std::string cxx;      // g++ / clang++ 的完整路径，空 = 没找到
    std::string cc;       // 同一套里的 C 编译器（cmake 要）
    std::string cmake;    // cmake 的完整路径，空 = 没找到
    std::string ninja;
    std::string vscode;   // VS Code 的 code 命令，用来「点报错跳过去」
    std::string kit;      // 找到的绿色工具箱根目录（空 = 不是从工具箱找到的）
    std::string source;   // 从哪找到的，写给学生看
    std::string version;  // 第一行版本号（异步取，可能是空的）
    std::string note;     // 找不到时的下一步怎么办
    // 起子进程前要临时插进 PATH 的目录（学生双击 exe 时 PATH 里没有工具箱）
    std::vector<std::string> binDirs;
    // 探测过程记下来的一行行说明（试过哪些候选、为什么没选中）——不管 verbose() 开没开
    // 都会记，只有打印的时候才看 verbose()（D-新：懒初始化只跑一次，重新探测代价不小，
    // 不如「一直记、按需打印」）。
    std::vector<std::string> diag;
};
const Toolchain& toolchain(bool refresh = false);
std::string      toolchainVersion();     // 阻塞最多 2 秒，只在自检里调

// ---------------------------------------------------------------- 启动横幅
// 「这是哪个 Easel、哪份预编译、编到哪去了」，一眼说清楚——工作台启动时打在输出区
// 最上面，--doctor 里也有，每次 build/run/package/export 的日志（包括写进
// .easel/last-build.log 的那份）开头都带上它。projectDir 留空 = 打「（没打开工程）」。
// 实现在 app.cpp（要用到 EASEL_GIT_SHA / EASEL_ABI / EASEL_BUILT_WITH 这几个只有
// 编译 easel 库时才看得见的宏）。
std::string banner(const std::string& projectDir = {});
// 字节数 -> "1.2 MB" 这种人话；秒数 -> "[  2.1s] " 这种右对齐的时间戳前缀。
// 编译产物大小、每一步日志的时间戳都靠它们，workbench 和 doctor() 都用得到。
std::string formatBytes(long long bytes);
std::string formatStamp(double seconds);

// ---------------------------------------------------------------- 详细模式
// --verbose（命令行）或工作台输出区上方的「详细」勾选。默认关：编译日志已经够看了，
// 平时不需要 PATH、候选路径这些排障细节；出问题时打开它，doctor() 和每次操作的日志
// 都会多打一截。resolvePaths() / toolchain() 的候选探测过程不管这个开关开没开都会
// 记下来（见上面 Toolchain::diag、editorPathsDiag()），这个开关只决定「打不打印」。
bool verbose();
void setVerbose(bool on);
// 起一个进程，等它结束再返回（超时就杀掉）。给自检这种一次性的活用，别在每帧里调。
bool runBlocking(const std::vector<std::string>& argv, const std::string& workDir, int timeoutMs,
                 std::string* out, int* exitCode,
                 const std::vector<std::string>* pathPrepend = nullptr);
// 在编辑器里打开某个文件的某一行（有 VS Code 就用它，没有就返回 false）
bool openInEditor(const std::string& file, int line, int col);
std::string      exePath();
std::string      exeDir();
std::string      joinPath(const std::string& a, const std::string& b);
std::string      baseName(const std::string& path);
std::string      absPath(const std::string& path);

// ---------------------------------------------------------------- 编辑栏（D-28）
struct EditorPaths {
    std::string projectDir;   // 学生工程根目录（编译期常量 EASEL_PROJECT_DIR，找不到就退回 cwd）
    std::string easelDir;     // Easel 源码树（EASEL_SOURCE_DIR）
    std::string file;         // 正在编辑的文件，默认 <project>/src/solver.cpp
    bool        ok = false;   // file 真的存在
};
// 「这个目录像不像一个学生工程根」：得同时含 src/app.cpp 和 src/solver.cpp 才算数。
// 光有 src/ 目录不够，甚至只查 app.cpp 也不够——Easel 仓库自己根目录下的 src/ 也有
// 一个 app.cpp（库内部实现，凑巧同名），但没有 solver.cpp。
// resolvePaths() 用它筛 projectDir 候选；测试直接调它验证判定本身。
bool               looksLikeProject(const std::string& dir);
const EditorPaths& editorPaths();
// resolvePaths() 试过的候选目录，一行一条（projectDir 和 easelDir 两段都在里面），
// verbose 模式下 doctor() 打出来。resolvePaths() 只跑一次，但这份记录跟着一起存好，
// 之后随时能打（不用重新探测一遍）。
const std::vector<std::string>& editorPathsDiag();
void               setEditorFile(const std::string& path);
void               drawEditor(App& app, const Rect& r, bool* open);
void               editorRun();          // F5：保存 → 编译 → 运行
void               editorPump();         // 每帧：把子进程的输出捞回来
bool               editorBusy();
// 从 g++ / clang 的一行诊断里抠出「文件:行:列」和严重程度。认不出来返回 false。
bool parseDiagnostic(const std::string& s, std::string* file, int* line, int* col, int* kind);

// ---------------------------------------------------------------- 导出工程（D-28 / D-29）
struct ExportOptions {
    std::string projectDir;         // 学生工程，空 = 用编译期常量推断（作品自己导出时）
    std::string easelDir;           // Easel 源码，空 = 同上
    std::string outDir;             // 导出到哪，空 = <project>/dist/<name>
    std::string name;               // 作品名，空 = 工程目录名
    bool        withEasel = true;   // 带上 Easel 源码与 vendor/（评委才编得出界面）
    bool        verify = true;      // 导出完试编一次 solver.cpp，证明它真的能独立编
};

// 从模板新建一个工程（工作台的「新建工程」，D-29）。
// templateDir 空 = <easelDir>/template。目录已存在且非空就拒绝，不覆盖任何东西。
struct NewProjectOptions {
    std::string easelDir;
    std::string templateDir;
    std::string parentDir;   // 建到哪个目录下
    std::string name;        // 作品名 = 目录名
    bool        withTests = false;    // 带一个 tests/test_solver.cpp（默认不带，要写测试再说）
    // 骨架：false = 空工程（一个文件，画个 Hello，D-32 / D-36）
    //       true  = 算法骨架（读数据文件、逐帧回放、收敛曲线、导出用例）
    bool        fullSkeleton = false;
    // 非空 = 从 <exampleDir>/main.cpp 建（单文件示例拷成 src/app.cpp，
    // exampleDir/assets、exampleDir/data 有就带上）。和 fullSkeleton 互斥（D-36）。
    std::string exampleDir;
};
struct NewProjectReport {
    bool        ok = false;
    std::string dir;
    std::string error;
    int         files = 0;
};
NewProjectReport createProject(const NewProjectOptions& opt);
struct ExportReport {
    bool                     ok = false;
    std::string              outDir;
    int                      files = 0;
    long long                bytes = 0;
    std::vector<std::string> checks;    // 每条以 [√] / [×] / [!] 开头
    std::string              error;
    std::string              checklist;  // 写进 CHECK.txt 的全文
};
ExportReport exportProject(const ExportOptions& opt);

}  // namespace internal
}  // namespace easel
#endif
