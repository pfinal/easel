// 代码酷工作台 —— 学生的入口（D-29）
//
// 分工照抄 Processing，编辑器换成 VS Code：
//   学生在 VS Code 里写自己的两个文件（界面 src/app.cpp、逻辑 src/solver.cpp）；
//   工作台负责其余全部 —— 新建工程、运行、停止、看报错、导出应用程序、导出工程、清理。
//   库、依赖、构建脚本由我们管，学生的 VS Code 里根本看不见它们。
//
// 关键一点：**作品是子进程**。所以「重新编译」永远不会撞上「正在运行的自己」——
// 这是 D-27 当初绕不过去的那个死结，换个进程模型就没了。
//
// 界面的每一条规则（用词、状态机、层级、两种模式、视觉档位）都写在 docs/ui-design.md 里，
// 这里只负责照着实现。**改界面之前先读那份文档**，尤其是第 0 节：内置字体子集里没有
// ✔ ✖ ▶ ■ ☐ 这类符号，界面上一个都不许出现（会变成豆腐块），状态靠汉字 + 颜色表达，
// 三角箭头用 ImGui 自己画的 ArrowButton。
#include <easel/easel.h>

#include "internal.h"

// 窗口几何（还原位置、紧凑模式改大小、始终置顶）要直接问 glfw。workbench 经
// easel_imgui 间接链接 glfw，头文件拿得到。App 现在不暴露窗口句柄，只能用
// glfwGetCurrentContext()（OpenGL 后端下就是主窗口；DX11 后端返回 nullptr，
// 那时这几个功能整体降级，见 docs/ui-design.md 第 4.5 节和第 7 节第 1 条）。
#include <GLFW/glfw3.h>

#include <cctype>
#include <climits>
#include <cstdio>
#include <cstring>
#include <iostream>

using namespace easel;
using namespace easel::internal;

namespace {

enum Stage { Idle, Configuring, Building, Running, Packing };

// ------------------------------------------------------------------ 视觉档位
// docs/ui-design.md 第 5 节的那张表，一一对应。界面里的每个尺寸都只能来自这里，
// 用的时候乘 dpi（draw() 收到的那个参数）。**不许在别处写裸数字。**
namespace M {
// 间距四档
constexpr float kGapTight = 6.f;     // 同一组里贴着的两个元素
constexpr float kGap = 10.f;         // 一般元素间距
constexpr float kPad = 14.f;         // 段内左右留白
constexpr float kGapSection = 20.f;  // 两个按钮之间、段内上下呼吸
// 段高
constexpr float kProjectH = 48.f;    // 工程栏
constexpr float kActionH = 76.f;     // 主操作区
constexpr float kSummaryH = 34.f;    // 摘要行（单行，永不长高）
constexpr float kBtnH = 44.f;        // 按钮高度，全界面统一
constexpr float kBtnRunW = 168.f;    // 运行
constexpr float kBtnStopW = 112.f;   // 停止
// 字号（乘 ImGui::GetStyle().FontSizeBase，只有这四档）
constexpr float kT1 = 1.25f;         // 工程名
constexpr float kT2 = 1.00f;         // 正文
constexpr float kT2b = 1.15f;        // 只给主按钮「运行」
constexpr float kT3 = 0.92f;         // 路径、次要说明、日志（等宽）
// 窗口
constexpr int kWinW = 960, kWinH = 600;            // 完整模式默认
constexpr int kCompactW = 520;                     // 紧凑模式宽度（高度按内容算）
constexpr int kMinFullW = 760, kMinFullH = 420;    // 最小尺寸
constexpr int kMinCompactW = 440, kMinCompactH = 200;
constexpr int kGrowMaxH = 420;                     // 紧凑模式失败时自动长高的上限
constexpr int kGrowLines = 6;                      // 最多展开几行错误
// 还原位置时那条「标题栏带」的尺寸要求（见 docs/ui-design.md 4.5）
constexpr int kTitleBandH = 32, kTitleBandMinW = 120, kTitleBandMinH = 24;
// 列表长度
constexpr int kRecentMax = 8;        // 「最近打开」记几条
constexpr int kRecentInEmpty = 3;    // 空状态里直接露几条
}  // namespace M

// 上一次操作的结果：状态机的第三个输入（另两个是「有没有工程」和 stage）。
enum Result { ResNone, ResOk, ResFail };

// 六个状态（docs/ui-design.md 第 2 节）。界面不许有第七种长相。
enum UiState {
    StEmpty,    // A 没打开任何工程
    StFresh,    // B 有工程，空闲，这次会话还没跑过
    StOk,       // C 有工程，空闲，上次成功
    StFail,     // D 有工程，空闲，上次失败
    StBusy,     // E 配置 / 编译 / 导出中
    StRunning   // F 作品运行中
};

struct OutLine {
    std::string text;
    int         kind = 0;   // 0 普通 1 错误 2 警告 3 我们自己说的话
    std::string file;
    int         line = 0, col = 0;
};

struct WB {
    std::string              projectDir;
    std::string              easelDir;
    std::vector<std::string> recent;

    Proc                 proc;
    int                  stage = Idle;
    std::string          pending;
    std::vector<OutLine> out;
    int                  errors = 0, warnings = 0;
    double               startedAt = 0;
    std::string          status = "先新建一个工程，或者打开一个已有的";
    char                 runArgs[192] = {0};

    // ---- 摘要行（docs/ui-design.md 第 2 节）：日志是证据，摘要是给人读的结论 ----
    int         lastResult = ResNone;
    std::string sumText;                 // 空 = 用当前状态的默认文案
    int         sumKind = 0;             // 0 次要灰 1 红 2 绿 3 黄
    std::string sumFile;                 // 非空 = 摘要行可点，跳到这个位置
    int         sumLine = 0, sumCol = 0;
    std::string busyText;                // 忙碌时摘要行的前缀（「编译中」「运行中 …」）
    double      busyFrom = 0;            // 忙碌开始的时刻（ImGui::GetTime()）
    std::string libNote;                 // 「用现成的库」/「从源码编界面库」，configure 跑完才知道
    std::string buildSummary;            // 上一次编译成功的那句结论，作品退出时要接着用

    // ---- 日志区 ----
    bool verboseUI = false;   // 「详细输出」勾选，同步 internal::verbose()
    bool stickBottom = true;  // 用户手动往上滚过之后就别再抢滚动条
    int  scrollTo = -1;       // 一次性：把视口滚到这一行（失败时指向第一条错误）
    int  firstError = -1;     // 第一条错误在 out 里的下标，-1 = 没有

    // ---- 两种模式 / 窗口几何（docs/ui-design.md 第 4 节）----
    bool compact = false;       // 紧凑模式：日志区收起
    bool alwaysOnTop = false;   // 始终置顶
    int  fullW = M::kWinW, fullH = M::kWinH;   // 完整模式的尺寸（逻辑像素，存盘的就是它）
    int  winX = INT_MIN, winY = INT_MIN;       // 上次的位置（屏幕坐标原值，INT_MIN = 没存过）
    int  fitFrames = 0;         // >0：紧凑模式下这几帧里把窗口高度贴合内容（一次性）
    int  lastSetH = 0;          // 我们上次设过的窗口高度，用来判断「用户自己拖过」
    bool grown = false;         // 紧凑模式因为失败而长高着

    // ---- 过渡意图：停掉正在跑的作品之后接着重跑（不是第七个状态）----
    bool pendingRun = false;
    // 导出工程是同步阻塞的，先画一帧「导出工程中」再真干活
    bool pendingExportProject = false;

    // 对话框：菜单项里只置旗标，菜单收起来之后再 OpenPopup
    bool openNewProject = false, openExportApp = false, openExportProject = false;
    bool openRunArgs = false, openAbout = false;
    char exportOutDir[512] = {0};

    // 「清理」菜单项的 tooltip 要说清删掉多大：文件菜单打开时算一次，别每帧去扫磁盘
    long long cleanBytes = -1;   // -1 = 还没算过

    // 「工程说清楚自己在做什么」：每次操作（运行 / 导出应用程序 / 导出工程 / 清理）从
    // 0.0s 重新起表，输出区里我们自己打的每一行都带 [ x.xs] 前缀；编译器/cmake/
    // 作品自己的原样输出不加前缀。opClock 在 beginLog() 里重置。
    Stopwatch            opClock;

    // 新建工程对话框
    char                     newName[128] = "MySketch";
    char                     newParent[512] = {0};
    bool                     newTests = false;
    // 骨架下拉：0 空白 / 1 算法骨架 / 2+ 示例（exampleNames[newSkeleton - 2]，D-36）
    int                      newSkeleton = 0;
    std::vector<std::string> exampleNames;

    bool wantPackage = false;   // 「导出应用程序」要多编一次 Release，分两步走
};

WB& wb() {
    static WB w;
    return w;
}

// ------------------------------------------------------------------ 小工具
const char* presetName() {
#if defined(_WIN32)
    return "mingw";
#else
    return "default";
#endif
}

// 新建工程对话框的默认父目录逻辑（D-36）：
// Windows：D:\ 存在 → D:\projects；否则 USERPROFILE\projects；再否则 cwd
// 其他平台：HOME/projects；拿不到 HOME 就 cwd
std::string defaultProjectsDir() {
#if defined(_WIN32)
    // Windows: 优先 D:\projects
    if (isDirU8("D:\\")) return "D:\\projects";

    // 次选：USERPROFILE\projects
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile && userProfile[0]) {
        return joinPath(userProfile, "projects");
    }
#else
    // 非 Windows：优先 HOME/projects
    const char* home = std::getenv("HOME");
    if (home && home[0]) {
        return joinPath(home, "projects");
    }
#endif

    // 备选方案：cwd
    return fs::cwd();
}

// 新式工程（D-30）：构建脚本和库都在 .easel/ 里，学生的根目录只有自己的文件。
// 老式工程（模板直接拷出来的、导出包）：根目录有 CMakeLists.txt，走 preset。
bool newStyle(const std::string& proj) {
    return existsU8(joinPath(proj, ".easel/CMakeLists.txt"));
}

std::string cmakeSrcDir(const std::string& proj) {
    return newStyle(proj) ? joinPath(proj, ".easel") : proj;
}

std::string buildDir(const std::string& proj) {
    return newStyle(proj) ? joinPath(proj, ".easel/build")
                          : joinPath(joinPath(proj, "build"), presetName());
}

std::string releaseDir(const std::string& proj) {
    return newStyle(proj) ? joinPath(proj, ".easel/build-release")
                          : joinPath(joinPath(proj, "build"), "release");
}

std::string exeIn(const std::string& dir) {
    std::string p = joinPath(joinPath(dir, "bin"), "app");
#if defined(_WIN32)
    p += ".exe";
#endif
    return p;
}

std::string appExe(const std::string& proj) { return exeIn(buildDir(proj)); }

// 配置一个构建目录要带的参数。指名道姓地把编译器和 ninja 传进去 ——
// 光靠 PATH 注入在 Windows 上不够稳（cmake 会记住第一次找到的编译器）。
std::vector<std::string> configureArgs(const std::string& proj, const std::string& build,
                                       const char* buildType) {
    const Toolchain&         tc = toolchain();
    std::vector<std::string> a{tc.cmake.empty() ? "cmake" : tc.cmake, "-S", cmakeSrcDir(proj),
                               "-B", build, std::string("-DCMAKE_BUILD_TYPE=") + buildType};
    if (!tc.ninja.empty()) {
        a.push_back("-G");
        a.push_back("Ninja");
        a.push_back("-DCMAKE_MAKE_PROGRAM=" + tc.ninja);
    }
    if (!tc.cxx.empty()) a.push_back("-DCMAKE_CXX_COMPILER=" + tc.cxx);
    if (!tc.cc.empty()) a.push_back("-DCMAKE_C_COMPILER=" + tc.cc);
    if (!wb().easelDir.empty()) a.push_back("-DEASEL_DIR=" + wb().easelDir);
    return a;
}

// 构建目录已经 configure 过时，从它的 CMakeCache.txt 里读上一次写进去的 EASEL_DIR，
// 好跟这次的 wb().easelDir 比一比（见下面 easelDirStale()）。缓存行形如
// `EASEL_DIR:UNINITIALIZED=/path`，类型段不一定是 UNINITIALIZED，按 `EASEL_DIR:`
// 前缀匹配、取第一个 `=` 之后的内容。读不到文件或者压根没有这一项就返回空串。
std::string cachedEaselDir(const std::string& build) {
    std::string text;
    if (!readTextU8(joinPath(build, "CMakeCache.txt"), &text)) return {};
    size_t start = 0;
    while (start <= text.size()) {
        size_t      nl = text.find('\n', start);
        std::string line = text.substr(start, (nl == std::string::npos ? text.size() : nl) - start);
        if (line.rfind("EASEL_DIR:", 0) == 0) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) return line.substr(eq + 1);
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return {};
}

// 规范化后再比——absPath() 只管补 cwd，不处理尾部斜杠，这里顺手去掉，避免
// "/a/b" 和 "/a/b/" 被判成不一致，每次都白白多重新 configure 一次。
std::string normEaselPath(const std::string& p) {
    std::string a = p.empty() ? std::string() : absPath(p);
    while (a.size() > 1 && (a.back() == '/' || a.back() == '\\')) a.pop_back();
    return a;
}

// 构建目录已经 configure 过（有 CMakeCache.txt）时，判断现在的 easelDir 跟缓存里那份
// 是否还对得上：`cmake --build` 不会重新读 -D 参数，CMakeCache.txt 里第一次写进去的
// EASEL_DIR 会一直有效。不一致——或者缓存里压根没有这一项、而现在有值——就要重新
// configure 一次；开发机上碰巧两份都在，顶多白编几分钟，仓库被挪走或删掉之后会
// 直接编不出来，而且报错很难懂。三处走「有缓存就跳过 configure」快路径的调用点
// （GUI 的 compileAndRun()、无头的 runBuild()、无头 --package 的 cmdPackage()）都要用它。
bool easelDirStale(const std::string& build, const std::string& easelDir) {
    // 这次压根不知道 Easel 在哪（easelDir 空，configureArgs() 就不会传 -DEASEL_DIR）：
    // 别去动一个已经配好、能编的构建目录，否则等于把缓存里那份可用的路径白扔掉。
    if (easelDir.empty()) return false;
    return normEaselPath(cachedEaselDir(build)) != normEaselPath(easelDir);
}

// 一个构建目录是不是「真配置好了，能直接 --build」：光有 CMakeCache.txt 不够——
// configure 失败（比如缺 OpenGL/X11/GTK 开发包）也会把 CMakeCache.txt 写出来，
// 但生成器的构建文件（Makefiles 生成器是 Makefile，Ninja 生成器是 build.ninja）
// 不会跟着生成。只查 CMakeCache.txt 的话，第一次 configure 失败之后这个构建目录
// 就再也自己好不了——`cmake --build` 会直接撞「没有规则可制作目标 "Makefile"」，
// 且没有任何提示告诉用户「删掉这个目录重来」。两种生成器都要认，两个都没有才
// 说明真没配置过。三处走「有缓存就跳过 configure」快路径的调用点（GUI 的
// compileAndRun()、无头的 runBuild()、无头 --package 的 cmdPackage()）都要用它，
// 不能各自查一遍 CMakeCache.txt。
bool buildConfigured(const std::string& build) {
    if (!existsU8(joinPath(build, "CMakeCache.txt"))) return false;
    return existsU8(joinPath(build, "Makefile")) || existsU8(joinPath(build, "build.ninja"));
}

std::string configPath() { return joinPath(exeDir(), "workbench.json"); }

// workbench.json：最近工程 + 窗口几何 + 「详细输出」开关（docs/ui-design.md 4.4）。
// 缺字段一律用默认值，解析失败整块丢掉——配置文件坏了不该让工作台打不开。
// 运行参数**故意不存**：它是临时的，存下来会变成「下次打开莫名带着参数跑」这种看不见的状态。
void loadConfig() {
    WB&  w = wb();
    json j = json::parse(fs::readText(configPath()), nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    if (j.contains("recent") && j["recent"].is_array())
        for (const json& x : j["recent"])
            if (x.is_string() && existsU8(x.get<std::string>())) w.recent.push_back(x);
    if (!w.recent.empty()) w.projectDir = w.recent.front();
    if (j.contains("verbose") && j["verbose"].is_boolean()) w.verboseUI = j["verbose"];
    if (j.contains("window") && j["window"].is_object()) {
        const json& g = j["window"];
        if (g.contains("w") && g["w"].is_number_integer()) w.fullW = g["w"];
        if (g.contains("h") && g["h"].is_number_integer()) w.fullH = g["h"];
        if (g.contains("x") && g["x"].is_number_integer()) w.winX = g["x"];
        if (g.contains("y") && g["y"].is_number_integer()) w.winY = g["y"];
        if (g.contains("compact") && g["compact"].is_boolean()) w.compact = g["compact"];
        if (g.contains("alwaysOnTop") && g["alwaysOnTop"].is_boolean()) w.alwaysOnTop = g["alwaysOnTop"];
        // 尺寸只做下限保护。上限是 src/backend.cpp 的 computeWindowGeometry() 的活
        // （它会等比夹进显示器可用区域），这里再夹一遍就是第二份实现。
        if (w.fullW < M::kMinFullW) w.fullW = M::kMinFullW;
        if (w.fullH < M::kMinFullH) w.fullH = M::kMinFullH;
    }
}

void saveConfig() {
    WB&  w = wb();
    json j;
    j["recent"] = w.recent;
    j["verbose"] = w.verboseUI;
    json g;
    g["w"] = w.fullW;
    g["h"] = w.fullH;
    if (w.winX != INT_MIN) g["x"] = w.winX;
    if (w.winY != INT_MIN) g["y"] = w.winY;
    g["compact"] = w.compact;
    g["alwaysOnTop"] = w.alwaysOnTop;
    j["window"] = g;
    fs::writeText(configPath(), j.dump(2));
}

void useProject(const std::string& dir) {
    WB& w = wb();
    if (dir.empty()) return;
    w.projectDir = dir;
    w.recent.erase(std::remove(w.recent.begin(), w.recent.end(), dir), w.recent.end());
    w.recent.insert(w.recent.begin(), dir);
    if (w.recent.size() > (size_t)M::kRecentMax) w.recent.resize(M::kRecentMax);
    saveConfig();
    w.status = "工程：" + baseName(dir);
    // 换工程 = 上一次编译的结论不再成立（那是另一个工程的事）
    w.lastResult = ResNone;
    w.sumText.clear();
    w.sumFile.clear();
    w.cleanBytes = -1;
}

void addOut(const std::string& text, int kind, const std::string& file = {}, int line = 0,
            int col = 0) {
    WB&     w = wb();
    OutLine o;
    o.text = text;
    o.kind = kind;
    o.file = file;
    o.line = line;
    o.col = col;
    w.out.push_back(std::move(o));
    if (w.out.size() > 4000) w.out.erase(w.out.begin(), w.out.begin() + 200);
}

void say(const std::string& s) { addOut(s, 3); }

// 我们自己打的一行，带 [ x.xs] 前缀（相对 opClock 的秒数）——跟编译器/cmake/作品的
// 原样输出（feed() 灌进来的那些，没有前缀）一眼能分清谁说的话。
void sayTS(const std::string& s, int kind = 3) {
    addOut(internal::formatStamp(wb().opClock.s()) + s, kind);
}

// 每次操作的完整输出追加写到 <工程>/.easel/last-build.log（覆盖式，只留最近一次）。
// 没打开工程（没地方写）就什么都不做。写整个 w.out 一份份拼出来，简单可靠——
// 一次操作最多几千行，拼一次字符串的开销可以忽略。
void flushLog() {
    WB& w = wb();
    if (w.projectDir.empty()) return;
    std::string text;
    for (const OutLine& o : w.out) text += o.text + "\n";
    writeTextU8(joinPath(w.projectDir, ".easel/last-build.log"), text);
}

// 每次操作开头都打一遍：清空输出区，起表，把启动横幅（不带时间戳，就是第 1 部分
// 那几行）打在最上面。GUI 启动时也单独调一次（那时候 w.projectDir 可能还是空的，
// 横幅会打「（没打开工程）」）。
void beginLog() {
    WB& w = wb();
    w.out.clear();
    w.errors = w.warnings = 0;
    w.opClock.reset();
    w.stickBottom = true;   // 新的一次操作：滚动条重新跟着输出走
    w.scrollTo = -1;
    w.libNote.clear();      // 「用现成的库 / 从源码编」要等这次的 configure 跑完才知道
    w.cleanBytes = -1;      // 构建产物大小变了，菜单里的数字下次打开时重算
    w.firstError = -1;
    w.sumFile.clear();
    std::string b = internal::banner(w.projectDir);
    size_t start = 0;
    while (start < b.size()) {
        size_t nl = b.find('\n', start);
        std::string line = b.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!line.empty()) addOut(line, 3);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
}

// --verbose / 「详细」勾选打开时，在操作开头多打一截排障细节：完整 PATH、子进程工作
// 目录、easel-prebuilt.json / VERSION.json 的原文。候选路径的探测过程（resolvePaths /
// 工具链）留给 --doctor --verbose——那边是 App::doctor() 唯一会去调 resolvePaths() 的
// 地方，这里不重复一遍。GUI（sayTS）和无头命令（自己拼文本）共用这份内容。
std::vector<std::string> verboseEnvLines(const std::string& dir) {
    std::vector<std::string> out;
    if (!internal::verbose()) return out;
    const Toolchain& tc = toolchain();
    out.push_back("详细：子进程工作目录 " + dir);
    out.push_back("详细：子进程 PATH 注入 " + Proc::describe(tc.binDirs));
    const char* pathEnv = std::getenv("PATH");
    out.push_back(std::string("详细：PATH = ") + (pathEnv ? pathEnv : "(未设置)"));
    if (!wb().easelDir.empty()) {
        for (const char* rel : {"prebuilt/easel-prebuilt.json", "build/default/prebuilt/easel-prebuilt.json",
                                "VERSION.json"}) {
            std::string p = joinPath(wb().easelDir, rel);
            std::string txt;
            if (readTextU8(p, &txt)) out.push_back("详细：" + p + " = " + txt);
        }
    }
    return out;
}

void logVerboseEnv(const std::string& dir) {
    for (const std::string& l : verboseEnvLines(dir)) sayTS(l);
}

// 预编译 abi 不匹配时，cmake 自己会打一句 STATUS（new_project.cpp 生成的 CMakeLists
// 里那句「改用源码编」），但那是 cmake 的原样输出，不带我们自己的「怎么办」。这里从
// 一段 cmake 输出里认出这句话，补一行人话提示（D-新，第 4 部分）。
void hintIfPrebuiltMismatch(const std::string& cmakeRaw) {
    if (cmakeRaw.find("改用源码编") != std::string::npos)
        sayTS("已改用源码编（约 3 分钟）。要恢复秒级编译，在目标平台重新生成预编译包", 2);
}

// 「这次到底走不走预编译」只有 configure 真正跑完、看过 cmake 的原样输出才知道——
// new_project.cpp 生成的 CMakeLists（kCMake）里那两句 STATUS：「用预编译的 Easel」
// 走预编译，几秒钟链上；「用本地的 Easel 源码」是第一次要把整个界面库编出来，
// 几分钟起步。配置还没开始就说「几分钟」是对着开发机说话——学生用发布包，摆在
// 那儿的预编译多数时候是能用的，正常应该几秒钟就编完，说「几分钟」白吓人。
// FetchContent 联网拉那条没有这两句 STATUS 可认，认不出来就不瞎猜，什么都不说。
void hintBuildSpeed(const std::string& cmakeRaw) {
    if (cmakeRaw.find("用预编译的 Easel") != std::string::npos) {
        sayTS("用的是预编译库，这次编译只要几秒。");
        wb().libNote = "用现成的库";
    } else if (cmakeRaw.find("用本地的 Easel 源码") != std::string::npos) {
        sayTS("这次要把界面库从源码编出来，得几分钟；以后就只编你改的那几行，会快很多。");
        wb().libNote = "从源码编界面库";
    }
}

// Linux 上最常见的 configure 失败根因：图形相关的 -dev 包没装。发布包只保证「解压
// 就能跑」（运行时共享库够用），但「新建工程之后编译」哪怕链的是预编译库，链接期
// 仍然要在本机核对 OpenGL / X11 / GTK 这些库存不存在（cmake/easelConfig.cmake.in 里
// find_dependency(OpenGL) 那几行；从源码编还要多过 vendor/glfw 的 find_package(X11)
// 和 vendor/nfd 的 pkg_check_modules(gtk+-3.0)）——精简安装 / 服务器版 / 容器大多只有
// 运行时那一档，没有 -dev 那一档，一编就在这几处炸。
// 三种关键字是在 ubuntu:22.04 容器里卸掉这些 -dev 包、真跑 cmake 抓出来的原样文字：
//   OpenGL "Could NOT find OpenGL (missing: OPENGL_glx_LIBRARY OPENGL_INCLUDE_DIR)"
//   X11    "Could NOT find X11 (missing: X11_X11_LIB)" / glfw 自己那句
//          "XKB headers not found; install X11 development package"
//   GTK    "Checking for module 'gtk+-3.0'" 后面跟 "No package 'gtk+-3.0' found"
// 包名清单照抄 .github/workflows/ci.yml「装 Linux 上的图形依赖」那个 step，不自己编——
// 那个 step 是 CI 每次真跑通的，保真。命中哪一类都给同一条命令：反正要装就一次装全，
// 省得学生缺一个装一个、来回点好几轮「编译」。命令必须带 `apt-get update`——本地
// 索引比已装的运行时库旧的机器上，跳过 update 直接 install 会撞一堆解不开的版本
// 依赖（比如 libbz2-dev 要求精确版本号的 libbz2-1.0，而索引和已装的差一个 -updates
// 补丁号），装的时候比原来那条 CMake 报错还难懂。
// 这条命令本身也可能装不上：如果本机 apt 源缺 `<代号>-updates` 仓库（换过源、或者
// 只同步了部分仓库的镜像——装机工具/国内镜像都可能踩到），装出来的运行时库版本会
// 比仓库里能配到的 -dev 包新，一样解不开依赖，报的是「依赖: … 但是 … 正要被安装」
// 这种版本对不上的错（实测 Ubuntu 24.04：机器上只配了 noble/noble-security，缺
// noble-updates，libdbus-1-dev/libicu-dev/libzstd-dev 全那样炸；补上 noble-updates
// 就一次装成功）。这条比原来的 CMake 报错更难懂，得单独提一句兜底，但只是兜底——
// 别喧宾夺主，一句话，别写成教程。
// GUI（sayTS 一行一行打）和无头命令（stamp 一行一行拼进 pretty）打印的方式不一样，
// 检测本身只写一份：命中就把「解释」「照抄的命令」「装不上时的兜底」各填回一个
// out 参数，没命中返回 false，调用方什么都不打。
bool detectMissingLinuxDeps(const std::string& cmakeRaw, std::string* explain, std::string* command,
                            std::string* fallback) {
#if defined(__linux__)
    bool missingOpenGL = cmakeRaw.find("Could NOT find OpenGL") != std::string::npos;
    bool missingX11 = cmakeRaw.find("Could NOT find X11") != std::string::npos ||
                      cmakeRaw.find("XKB headers not found") != std::string::npos;
    bool missingGtk = cmakeRaw.find("gtk+-3.0") != std::string::npos;
    if (!missingOpenGL && !missingX11 && !missingGtk) return false;
    *explain = "这台机器像是缺图形相关的开发库（OpenGL/X11/GTK 的 -dev 包没装——发布包只带了"
               "运行时共享库，链接期还要核对这些库的头文件和无版本号 .so 在不在）。装这一行，"
               "再编一次：";
    *command = "$ sudo apt-get update && sudo apt-get install -y libgl1-mesa-dev libx11-dev "
               "libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxkbcommon-dev "
               "libgtk-3-dev ninja-build";
    *fallback = "这条要是装不上、报的是「依赖: … 但是 … 正要被安装」这种版本对不上的错，多半是软件源"
                "缺 -updates 仓库——查一下 /etc/apt/sources.list.d/ubuntu.sources 里 Suites: 那一行有没有"
                "「你的版本代号-updates」（比如 Ubuntu 24.04 是 noble-updates）。";
    return true;
#else
    (void)cmakeRaw;
    (void)explain;
    (void)command;
    (void)fallback;
    return false;
#endif
}

void feed(const std::string& chunk) {
    WB& w = wb();
    w.pending += chunk;
    size_t start = 0;
    for (;;) {
        size_t nl = w.pending.find('\n', start);
        if (nl == std::string::npos) break;
        std::string line = w.pending.substr(start, nl - start);
        start = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string f;
        int         ln = 0, cl = 0, kind = 0;
        if (parseDiagnostic(line, &f, &ln, &cl, &kind)) {
            if (kind == 1) ++w.errors;
            if (kind == 2) ++w.warnings;
            // 相对路径补成绝对的，VS Code 才跳得准
            if (!f.empty() && f[0] != '/' && f.find(':') == std::string::npos)
                f = joinPath(w.projectDir, f);
            addOut(line, kind, f, ln, cl);
        } else {
            addOut(line, 0);
        }
    }
    w.pending.erase(0, start);
}

// ------------------------------------------------------------------ 摘要行
// 「2.5 秒」这种人话时长。摘要行给人读，不用 formatStamp() 那种 [ 2.5s] 的日志格式。
std::string secText(double seconds) {
    char buf[32];
    if (seconds < 100)
        std::snprintf(buf, sizeof buf, "%.1f 秒", seconds);
    else
        std::snprintf(buf, sizeof buf, "%d 秒", (int)(seconds + 0.5));
    return buf;
}

// 日志是证据，摘要行是给人读的结论（docs/ui-design.md 第 2 节、4.1）。两者不互相替代：
// 这里只管「一句话说清刚才怎么样了」，原始输出一行不少地留在日志里。
void setSummary(const std::string& text, int kind) {
    WB& w = wb();
    w.sumText = text;
    w.sumKind = kind;
    w.sumFile.clear();
    w.sumLine = w.sumCol = 0;
}

// 失败时的摘要行：要说人话，而且能点（点了跳到第一条错误，跟点日志里那一行等价）。
void setSummaryError(const std::string& text, const std::string& file, int line, int col) {
    WB& w = wb();
    setSummary(text, 1);
    w.sumFile = file;
    w.sumLine = line;
    w.sumCol = col;
}

// 忙碌（配置 / 编译 / 导出 / 运行）时摘要行显示「<前缀> 3.1 秒」，秒数每帧自己长。
void setBusy(const std::string& prefix) {
    WB& w = wb();
    w.busyText = prefix;
    w.busyFrom = ImGui::GetTime();
    w.sumFile.clear();
}

// 日志里第一条错误（失败时摘要行要指向它，紧凑模式要展开它）。
void findFirstError() {
    WB& w = wb();
    w.firstError = -1;
    for (size_t i = 0; i < w.out.size(); ++i)
        if (w.out[i].kind == 1 && !w.out[i].file.empty() && w.out[i].line > 0) {
            w.firstError = (int)i;
            return;
        }
    // 没有带位置的错误行，退一步指向任何一条错误行（至少能把视口滚到出事的地方）
    for (size_t i = 0; i < w.out.size(); ++i)
        if (w.out[i].kind == 1) {
            w.firstError = (int)i;
            return;
        }
}

// 失败收尾统一走这里：摘要行说人话 + 把视口滚到第一条错误（不是底部——底部只是重复）。
void failed(const std::string& text) {
    WB& w = wb();
    findFirstError();
    if (w.firstError >= 0) {
        const OutLine& o = w.out[(size_t)w.firstError];
        setSummaryError(text + (o.file.empty() ? "" : " · 点这里看第一个"), o.file, o.line, o.col);
        w.scrollTo = w.firstError;
    } else {
        setSummary(text, 1);
    }
    w.lastResult = ResFail;
    w.busyText.clear();
    w.stickBottom = false;   // 视口要停在第一条错误那儿，不是底部
}

// 跳到代码里某一行（错误行、摘要行都用它）。没装 VS Code 就把位置复制到剪贴板。
void jumpTo(const std::string& file, int line, int col) {
    if (file.empty() || line <= 0) return;
    if (openInEditor(file, line, col)) return;
    std::string loc = file + ":" + std::to_string(line);
    ImGui::SetClipboardText(loc.c_str());
    addOut("没找到 VS Code，位置已复制：" + loc, 2);
}

// ------------------------------------------------------------------ 清理
// 「清理」删的是可以重新生成的构建产物，**只有这两个目录**，路径一律问 buildDir() /
// releaseDir()（新式工程在 .easel/ 下，老式工程在 build/ 下，两套布局都要认）。
// 绝对不删 dist/ —— 那是「导出应用程序」「导出工程」的产出，是交付物不是垃圾，而且不保证
// 还能原样重新生成（当时的 Easel 版本、assets 都可能变了）。Xcode 的 Clean 也不动你的归档。
// 也不删 .easel/last-build.log（上一次失败的现场证据）和学生自己的任何文件。
// 详见 docs/ui-design.md 6.3。GUI 和无头 --clean 共用这一份实现。
long long dirSizeU8(const std::string& dir) {
    long long total = 0;
    for (const DirEntry& e : listDirU8(dir)) {
        std::string p = joinPath(dir, e.name);
        if (e.isDir) {
            total += dirSizeU8(p);
        } else {
            long long sz = 0;
            if (fileStamp(p, nullptr, &sz)) total += sz;
        }
    }
    return total;
}

std::vector<std::string> cleanTargets(const std::string& proj) {
    return {buildDir(proj), releaseDir(proj)};
}

// 两个构建目录加起来多大（不存在就算 0）。菜单项的 tooltip 和无头命令都用它。
long long cleanableBytes(const std::string& proj) {
    long long total = 0;
    for (const std::string& d : cleanTargets(proj))
        if (isDirU8(d)) total += dirSizeU8(d);
    return total;
}

struct CleanReport {
    bool                     ok = true;
    std::vector<std::string> removed;    // 真删掉的目录
    long long                bytes = 0;  // 腾出多少字节
    std::string              error;
};

// 幂等：目录本来就不存在不算失败（脚本敢连着调两次）。
CleanReport cleanProject(const std::string& proj) {
    CleanReport rep;
    for (const std::string& d : cleanTargets(proj)) {
        if (!isDirU8(d)) continue;
        long long sz = dirSizeU8(d);
        if (!removeTreeU8(d)) {
            rep.ok = false;
            rep.error = "删不掉 " + d + "（是不是有程序正占着里面的文件？）";
            return rep;
        }
        rep.removed.push_back(d);
        rep.bytes += sz;
    }
    return rep;
}

// ------------------------------------------------------------------ 窗口几何
// App 不暴露窗口句柄，只能问 glfw 要当前上下文那个窗口（OpenGL 后端下就是主窗口）。
// DX11 后端返回 nullptr：位置还原 / 置顶 / 紧凑模式改大小整体降级，布局本身不受影响。
GLFWwindow* wbWindow() { return glfwGetCurrentContext(); }

// 「上次存下来的位置，在现在这套显示器上还看得见吗」——只回答能不能用，**不修正、不夹取**。
// 夹尺寸是 src/backend.cpp 的 computeWindowGeometry() 的活，这里写第二份就是重复实现。
// 只验窗口顶部那条标题栏带：能抓住标题栏，用户就能把窗口拖回来（docs/ui-design.md 4.5）。
bool positionStillVisible(int x, int y, int w, int h) {
    int count = 0;
    GLFWmonitor** mons = glfwGetMonitors(&count);
    if (!mons || count <= 0) return false;
    int bandH = M::kTitleBandH;
    if (bandH > h) bandH = h;
    for (int i = 0; i < count; ++i) {
        int mx = 0, my = 0, mw = 0, mh = 0;
        glfwGetMonitorWorkarea(mons[i], &mx, &my, &mw, &mh);
        int ix = std::max(x, mx), iy = std::max(y, my);
        int ax = std::min(x + w, mx + mw), ay = std::min(y + bandH, my + mh);
        if (ax - ix >= M::kTitleBandMinW && ay - iy >= M::kTitleBandMinH) return true;
    }
    return false;
}

// 启动时还原位置：验过了才 setPos，验不过什么都不做（窗口留在 backend 居中好的位置）。
void restoreWindowPos() {
    WB&         w = wb();
    GLFWwindow* win = wbWindow();
    if (!win || w.winX == INT_MIN || w.winY == INT_MIN) return;
    int cw = 0, ch = 0;
    glfwGetWindowSize(win, &cw, &ch);
    if (!positionStillVisible(w.winX, w.winY, cw, ch)) {
        // 换过外接屏 / 换过分辨率：这个位置现在看不见了，丢掉它
        sayTS("上次的窗口位置在现在的显示器上看不见了，这次居中打开");
        w.winX = w.winY = INT_MIN;
        return;
    }
    glfwSetWindowPos(win, w.winX, w.winY);
}

void applyAlwaysOnTop() {
    GLFWwindow* win = wbWindow();
    if (win) glfwSetWindowAttrib(win, GLFW_FLOATING, wb().alwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
}

void applySizeLimits(float dpi) {
    GLFWwindow* win = wbWindow();
    if (!win) return;
    const WB& w = wb();
    int       mw = (int)((w.compact ? M::kMinCompactW : M::kMinFullW) * dpi);
    int       mh = (int)((w.compact ? M::kMinCompactH : M::kMinFullH) * dpi);
    glfwSetWindowSizeLimits(win, mw, mh, GLFW_DONT_CARE, GLFW_DONT_CARE);
}

// ------------------------------------------------------------------ 编译 / 运行
bool spawn(const std::vector<std::string>& argv, const std::string& cwd) {
    WB&              w = wb();
    const Toolchain& tc = toolchain();
    std::string      err;
    if (!w.proc.start(argv, cwd, &err, &tc.binDirs)) {
        addOut(err, 1);
        w.stage = Idle;
        return false;
    }
    w.startedAt = ImGui::GetTime();
    return true;
}

// 打「构建目录 ...（Ninja · RelWithDebInfo）」+「$ cmake ...」+（--verbose 才有的）
// 环境细节。startConfigure()（平时编译）和 startPackage()（导出应用程序的 Release 配置）
// 共用——两边参数不一样，但「说清楚在哪、编成什么样」这句台词是同一句。
void logConfigureStart(const std::string& build, const char* buildType,
                       const std::vector<std::string>& argv) {
    WB& w = wb();
    sayTS("构建目录 " + build + "（" + (toolchain().ninja.empty() ? "Unix Makefiles" : "Ninja") + " · " +
          buildType + "）");
    logVerboseEnv(w.projectDir);
    sayTS("$ " + Proc::describe(argv));
}

bool cmakeMissing() {
    if (!toolchain().cmake.empty()) return false;
    sayTS("cmake 没找到 —— 装 CMake（brew install cmake / 工具箱里已自带）或把它加进 PATH", 1);
    return true;
}

void startConfigure() {
    WB&                      w = wb();
    std::string              build = buildDir(w.projectDir);
    std::vector<std::string> argv = configureArgs(w.projectDir, build, "RelWithDebInfo");
    logConfigureStart(build, "RelWithDebInfo", argv);
    // 这次到底走预编译（几秒）还是从源码编（几分钟）要等 configure 跑完才知道——
    // 见下面 hintBuildSpeed()，提示挪到配置完成之后打。
    w.stage = Configuring;
    if (!spawn(argv, w.projectDir)) return;
    w.status = "配置中…";
    setBusy(w.wantPackage ? "生成应用程序中" : "编译中");
}

void startBuild() {
    WB&              w = wb();
    const Toolchain& tc = toolchain();
    // 只编 app：solver 和 tests 是学生自己在命令行 / 编辑器里跑的，这里编它们只是白等
    std::vector<std::string> argv{tc.cmake.empty() ? "cmake" : tc.cmake, "--build",
                                  buildDir(w.projectDir), "--target", "app", "--parallel"};
    sayTS("$ " + Proc::describe(argv));
    w.stage = Building;
    if (!spawn(argv, w.projectDir)) return;
    w.status = "编译中…";
    setBusy("编译中");
}

void startApp() {
    WB&                      w = wb();
    std::vector<std::string> argv{appExe(w.projectDir)};
    std::string              a = w.runArgs, cur;
    for (size_t i = 0; i <= a.size(); ++i) {
        if (i == a.size() || a[i] == ' ') {
            if (!cur.empty()) argv.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(a[i]);
        }
    }
    if (!existsU8(argv[0])) {
        sayTS("没找到编出来的程序：" + argv[0], 1);
        w.stage = Idle;
        failed("失败 没找到编出来的程序");
        return;
    }
    sayTS("$ " + Proc::describe(argv) + "（作品的窗口会自己弹出来）");
    w.stage = Running;
    if (!spawn(argv, w.projectDir)) return;
    w.status = "运行中 —— 作品在另一个窗口里";
    setBusy("运行中 作品在另一个窗口里");
}

void compileAndRun() {
    WB& w = wb();
    if (w.projectDir.empty()) {
        say("先新建或者打开一个工程。");
        return;
    }
    if (w.proc.running()) {
        say("上一个还在跑，先按「停止」。");
        return;
    }
    w.pendingRun = false;
    beginLog();
    w.pending.clear();
    w.wantPackage = false;
    sayTS("工程 " + w.projectDir);
    if (cmakeMissing()) { flushLog(); return; }
    std::string build = buildDir(w.projectDir);
    if (buildConfigured(build)) {
        if (easelDirStale(build, w.easelDir)) {
            sayTS("Easel 位置变了，重新配置构建目录");
            startConfigure();
        } else {
            startBuild();
        }
    } else {
        startConfigure();
    }
    flushLog();
}

// 「运行」按钮和 F5 的唯一入口。作品正在跑的时候按下去 = 先掐掉它，再重新编译运行
// （Processing 的 Run 就是这个行为；理由见 docs/ui-design.md 2.2）。正在配置 / 编译时
// 按钮是灰的，走不到这儿。
void runOrRerun() {
    WB& w = wb();
    if (w.projectDir.empty()) return;
    if (w.stage == Running && w.proc.running()) {
        say("已停止上一次运行");
        w.proc.kill();        // 收尾仍然走 pump()（退出码 -2），那边看见 pendingRun 就接着重跑
        w.pendingRun = true;
        return;
    }
    if (w.proc.running()) return;   // 配置 / 编译中：按钮本来就是灰的，保险
    compileAndRun();
}

// 「打开日志文件」菜单项：优先让 VS Code 打开日志文件本身（能直接看内容），
// 没有 VS Code 就退到系统的「在文件管理器里定位这个文件」命令——file::folder()
// 只会弹目录选择对话框，不是我们要的「带我去看那个文件」，不合适。
void openLogFile(const std::string& logPath) {
    if (logPath.empty()) return;
    if (openInEditor(logPath, 0, 0)) return;
    std::vector<std::string> argv;
#if defined(_WIN32)
    argv = {"explorer.exe", "/select,", logPath};
#elif defined(__APPLE__)
    argv = {"open", "-R", logPath};
#else
    argv = {"xdg-open", fs::dirOf(logPath)};
#endif
    static Proc p;
    std::string err;
    p.start(argv, {}, &err);
}

// 「导出应用程序」共用的部分：把 Release 版程序 + assets + data 拷成一个能双击的目录。
// GUI 的「导出应用程序」（finishPackage()）和无头 `--package`（headless::cmdPackage）
// 都调它——两边前面配置/编译的方式不同（一个是异步 Proc + pump()，一个是同步
// runBlocking()），但编完之后「怎么收进 dist/」是同一套逻辑，不该抄两遍。
// outExeBytes 非空时回填交付出去那个可执行文件的大小——日志要打出来（第 2 部分）。
std::string packageInto(const std::string& projectDir, std::string* err, long long* outExeBytes = nullptr) {
    std::string name = baseName(projectDir);
    std::string dest = joinPath(joinPath(projectDir, "dist"), name + "-release");
    std::string exe = exeIn(releaseDir(projectDir));
    if (!existsU8(exe)) {
        if (err) *err = "没找到 Release 版的程序：" + exe;
        return {};
    }
    makeDirsU8(dest);
    int         files = 0;
    std::string destExe = joinPath(dest, name + (exe.size() > 4 && exe.substr(exe.size() - 4) == ".exe"
                                                     ? ".exe"
                                                     : ""));
    copyFileU8(exe, destExe);
    for (const char* d : {"assets", "data"})
        if (existsU8(joinPath(projectDir, d)))
            copyTreeU8(joinPath(projectDir, d), joinPath(dest, d), &files);
    writeTextU8(joinPath(dest, "README.txt"),
                name + "\n\n双击 " + name + " 就能运行。\n\n"
                "Windows 上如果弹出蓝色的「Windows 已保护你的电脑」：\n"
                "  点「更多信息」→「仍要运行」。这是没买代码签名证书的新程序的默认提示。\n\n"
                "macOS 上如果提示「无法验证开发者」：右键点它 →「打开」→ 再点一次「打开」。\n\n"
                "程序打不开或者画面不对，命令行里跑一下：" + name + " --doctor\n");
    if (outExeBytes) {
        long long sz = 0;
        fileStamp(destExe, nullptr, &sz);
        *outExeBytes = sz;
    }
    return dest;
}

// 「导出应用程序」：Release 编一遍，然后把程序 + assets + data 拷成一个能双击的目录
void finishPackage() {
    WB&         w = wb();
    std::string err;
    long long   bytes = 0;
    std::string dest = packageInto(w.projectDir, &err, &bytes);
    if (dest.empty()) {
        sayTS("导出失败：" + err, 1);
        failed("失败 导出应用程序没成功");
        flushLog();
        return;
    }
    sayTS("导出完成 -> " + dest + "（" + formatBytes(bytes) + "）");
    setSummary("完成 已导出应用程序 · " + baseName(dest) + " · " + formatBytes(bytes), 2);
    w.lastResult = ResOk;
    flushLog();
    if (App::instance()) App::instance()->toast("应用程序导出好了");
}

void startPackage() {
    WB& w = wb();
    if (w.projectDir.empty() || w.proc.running()) return;
    beginLog();
    w.wantPackage = true;
    sayTS("工程 " + w.projectDir);
    if (cmakeMissing()) {
        w.wantPackage = false;
        failed("失败 找不到 cmake");
        flushLog();
        return;
    }
    std::string              rel = releaseDir(w.projectDir);
    std::vector<std::string> argv = configureArgs(w.projectDir, rel, "Release");
    logConfigureStart(rel, "Release", argv);
    sayTS("导出交付用的应用程序：优化过、不带调试信息，比平时慢一点编。");
    w.stage = Configuring;
    if (!spawn(argv, w.projectDir)) return;
    w.status = "配置 Release…";
    setBusy("生成应用程序中");
    flushLog();
}

// 「导出工程」：exportProject() 是同步阻塞的（几十秒），所以分两步——先把「导出工程中」
// 画出去一帧（置 pendingExportProject），下一帧在 pump() 里才真干活。不这样的话用户看到
// 的是一个一动不动的假死窗口（docs/ui-design.md 2.1、第 7 节第 2 条）。
void beginExportProject() {
    WB& w = wb();
    if (w.projectDir.empty() || w.proc.running()) return;
    beginLog();
    std::string outDir = w.exportOutDir[0] ? std::string(w.exportOutDir)
                                           : joinPath(joinPath(w.projectDir, "dist"), baseName(w.projectDir));
    sayTS("源工程 " + w.projectDir);
    sayTS("导出到 " + outDir);
    logVerboseEnv(w.projectDir);
    sayTS("正在导出完整工程…（几十 MB，界面会卡一下）");
    setBusy("导出工程中");
    w.pendingExportProject = true;
    flushLog();
}

void finishExportProject() {
    WB& w = wb();
    w.pendingExportProject = false;
    ExportOptions o;
    o.projectDir = w.projectDir;
    o.easelDir = w.easelDir;
    o.name = baseName(w.projectDir);
    if (w.exportOutDir[0]) o.outDir = w.exportOutDir;
    ExportReport rep = exportProject(o);
    for (const std::string& l : rep.checks)
        addOut(l, l.rfind("[×]", 0) == 0 ? 1 : l.rfind("[!]", 0) == 0 ? 2 : 0);
    if (rep.ok) {
        sayTS("导出完成 -> " + rep.outDir + "（" + std::to_string(rep.files) + " 个文件，" +
              formatBytes(rep.bytes) + "）");
        setSummary("完成 已导出工程 · " + std::to_string(rep.files) + " 个文件 · " +
                       formatBytes(rep.bytes), 2);
        w.lastResult = ResOk;
    } else {
        sayTS("导出失败：" + rep.error, 1);
        failed("失败 导出工程没成功");
    }
    w.busyText.clear();
    flushLog();
}

// 「清理」：删两个构建目录，报腾出多少空间。不弹确认框（和 VS / Xcode / IntelliJ 一致：
// 删的是可以重新生成的东西），分寸靠菜单项的 tooltip + 这里的报告（docs/ui-design.md 6.3）。
void doClean() {
    WB& w = wb();
    if (w.projectDir.empty() || w.proc.running()) return;
    beginLog();
    sayTS("工程 " + w.projectDir);
    sayTS("清理构建产物（不动 dist/，也不动你的代码）");
    CleanReport rep = cleanProject(w.projectDir);
    for (const std::string& d : rep.removed) sayTS("删掉 " + d);
    if (!rep.ok) {
        sayTS(rep.error, 1);
        failed("失败 清理没做完");
    } else if (rep.removed.empty()) {
        sayTS("没有构建产物可清理（两个构建目录都不在）");
        setSummary("完成 清理 本来就没有构建产物", 2);
        w.lastResult = ResNone;
    } else {
        sayTS("清理完成，腾出 " + formatBytes(rep.bytes));
        setSummary("完成 清理 腾出 " + formatBytes(rep.bytes), 2);
        // 上一次编译的结论已经不成立了，挂着「完成 2.5 秒」是在说谎 → 回到「还没跑过」
        w.lastResult = ResNone;
    }
    w.cleanBytes = -1;
    flushLog();
}

void pump() {
    WB& w = wb();
    // 「导出工程」是同步阻塞的：上一帧已经把「导出工程中」画出去了，这一帧才真干活
    if (w.pendingExportProject) {
        finishExportProject();
        return;
    }
    if (w.stage == Idle && !w.proc.running()) return;

    std::string chunk;
    bool        alive = w.proc.pump(&chunk);
    if (!chunk.empty()) {
        feed(chunk);
        flushLog();
    }
    if (alive) return;
    if (!w.pending.empty()) {
        feed("\n");
        w.pending.clear();
    }

    int  code = w.proc.exitCode();
    char buf[256];

    if (w.stage == Configuring) {
        if (code != 0) {
            std::snprintf(buf, sizeof buf, "配置失败（退出码 %d）。上面几行是原因。", code);
            sayTS(buf, 1);
            cmakeMissing();
            {
                std::string cfgText;
                for (const OutLine& o : w.out) cfgText += o.text + "\n";
                std::string explain, command, fallback;
                if (detectMissingLinuxDeps(cfgText, &explain, &command, &fallback)) {
                    sayTS(explain, 1);
                    sayTS(command);
                    sayTS(fallback);
                }
            }
            w.status = "配置失败";
            w.stage = Idle;
            w.wantPackage = false;
            w.pendingRun = false;
            failed("失败 编译没能开始 · 看下面的输出");
            flushLog();
            return;
        }
        sayTS("配置完成");
        {
            std::string cfgText;
            for (const OutLine& o : w.out) cfgText += o.text + "\n";
            hintIfPrebuiltMismatch(cfgText);
            hintBuildSpeed(cfgText);
        }
        if (w.wantPackage) {
            const Toolchain&          tc = toolchain();
            std::string               rel = releaseDir(w.projectDir);
            std::vector<std::string>  argv{tc.cmake.empty() ? "cmake" : tc.cmake, "--build", rel,
                                           "--config", "Release", "--target", "app", "--parallel"};
            sayTS("$ " + Proc::describe(argv));
            w.stage = Building;
            spawn(argv, w.projectDir);
            w.status = "编译 Release…";
        } else {
            startBuild();
        }
        flushLog();
        return;
    }

    if (w.stage == Building) {
        if (code != 0) {
            if (w.errors == 0 && w.warnings == 0) {
                // 一个错误/警告都没有：构建系统本身没起来（链接器报的东西没被
                // parseDiagnostic 认出来、目标没找到之类），没有诊断可指，「点红色
                // 那行」等于让人点空气。把原始输出（不算我们自己打的那些行）结尾
                // 摆出来，好过什么都不给。
                sayTS("编译失败，但没有具体的错误/警告可以点——这类失败通常不是代码本身的"
                      "问题，是构建系统或者环境出了问题。原始输出最后几行：", 1);
                std::string rawTail;
                for (const OutLine& o : w.out)
                    if (o.kind == 0) rawTail += o.text + "\n";
                for (const std::string& l : tailNonEmptyLines(rawTail, 6)) addOut(l, 0);
            } else {
                std::snprintf(buf, sizeof buf, "编译失败：%d 个错误、%d 个警告。点红色那行跳到代码里。",
                              w.errors, w.warnings);
                sayTS(buf, 1);
            }
            w.status = "编译失败";
            w.stage = Idle;
            w.wantPackage = false;
            w.pendingRun = false;
            if (w.errors || w.warnings) {
                std::snprintf(buf, sizeof buf, "失败 %d 个错误、%d 个警告", w.errors, w.warnings);
                failed(buf);
            } else {
                failed("失败 编译没过，但没有具体的错误可以点（多半是环境问题）");
            }
            flushLog();
            return;
        }
        std::string exe = w.wantPackage ? exeIn(releaseDir(w.projectDir)) : appExe(w.projectDir);
        long long   sz = 0;
        fileStamp(exe, nullptr, &sz);
        std::snprintf(buf, sizeof buf, "编译完成 -> %s（%s）%s", exe.c_str(), formatBytes(sz).c_str(),
                      w.warnings ? ("，" + std::to_string(w.warnings) + " 个警告").c_str() : "");
        sayTS(buf);
        if (w.wantPackage) {
            w.wantPackage = false;
            w.stage = Idle;
            w.status = "就绪";
            finishPackage();
        } else {
            // 摘要行的结论：编了多久、走的哪条路、产物多大。作品退出时还要接着用它
            std::string sum = "完成 编译 " + secText(w.opClock.s());
            if (!w.libNote.empty()) sum += " · " + w.libNote;
            if (sz > 0) sum += " · " + formatBytes(sz);
            if (w.warnings) sum += " · " + std::to_string(w.warnings) + " 个警告";
            w.buildSummary = sum;
            setSummary(sum, w.warnings ? 3 : 2);
            w.lastResult = ResOk;
            startApp();
        }
        flushLog();
        return;
    }

    if (w.stage == Running) {
        if (code == 0) {
            sayTS("作品退出，退出码 0");
            setSummary((w.buildSummary.empty() ? std::string("完成") : w.buildSummary) + " · 作品已退出", 2);
            w.lastResult = ResOk;
        } else if (code == -2) {
            sayTS("已停止");
            setSummary("已停止" + (w.buildSummary.empty() ? std::string() : " · " + w.buildSummary), 0);
            w.lastResult = ResOk;
        } else if (code >= 128) {
            std::snprintf(buf, sizeof buf,
                          "作品被信号 %d 打断 —— 多半是越界或空指针。改完再跑一次；"
                          "要查具体哪一行，用 VS Code 的 F5 调试。",
                          code - 128);
            sayTS(buf, 1);
            std::snprintf(buf, sizeof buf, "失败 作品被信号 %d 打断（多半是越界或空指针）", code - 128);
            failed(buf);
        } else {
            std::snprintf(buf, sizeof buf, "作品退出，退出码 %d", code);
            sayTS(buf, 2);
            std::snprintf(buf, sizeof buf, "完成 作品退出，退出码 %d", code);
            setSummary(buf, 3);
            w.lastResult = ResOk;
        }
        w.stage = Idle;
        w.status = "就绪";
        w.busyText.clear();
        // 「运行中按运行 = 重跑」：上一个作品收完尾，这里接着走新的一轮（docs/ui-design.md 2.2）
        if (w.pendingRun) {
            w.pendingRun = false;
            compileAndRun();
        }
    }
    flushLog();
}

// ------------------------------------------------------------------ 无头命令行
// --build / --run / --package / --export：CI、脚本、老师批量操作用，跟 --new 是
// 同一类——main() 里在 App 构造之后、app.run() 之前处理，处理完直接 return，不开
// 窗口。复用上面配置/编译用的那几个函数（configureArgs/buildDir/appExe/...），
// 子进程一律走 runBlocking（这里不需要每帧 pump），超时给 600 秒
// （新式工程第一次编界面库要好几分钟）。
//
// --json 输出：只往 stdout 打一条 JSON，别的信息（找不到工程之类）一律进 stderr；
// 不带 --json 时原样透传子进程输出，末尾补一行人话摘要。
namespace headless {

constexpr int kTimeoutMs = 600000;

struct Diag {
    std::string severity, file, message;
    int         line = 0, col = 0;
};

std::string diagSeverity(int kind) { return kind == 1 ? "error" : kind == 2 ? "warning" : "note"; }

// parseDiagnostic()（src/editor.cpp）只给位置和严重程度，不单独给消息文本——
// 这里把关键字之后的那一段抠出来当 message。关键字表必须跟 parseDiagnostic 认的
// 那张表一致，不然位置对得上但摘出来的 message 会带着无关的前缀。
std::string diagMessage(const std::string& line) {
    static const char* const kws[] = {" error:", " 错误：", " fatal error:", " warning:",
                                      " 警告：", " note:", " 附注："};
    size_t best = std::string::npos, bestLen = 0;
    for (const char* k : kws) {
        size_t p = line.find(k);
        if (p != std::string::npos && (best == std::string::npos || p < best)) {
            best = p;
            bestLen = std::strlen(k);
        }
    }
    if (best == std::string::npos) return line;
    std::string msg = line.substr(best + bestLen);
    while (!msg.empty() && msg.front() == ' ') msg.erase(0, 1);
    return msg;
}

// 从一段编译器原样输出里抠出所有能认出来的诊断，顺手数错误/警告个数。相对路径
// 补成基于工程目录的绝对路径——JSON 是给脚本/编辑器用的，相对路径它们跳不准。
std::vector<Diag> collectDiagnostics(const std::string& raw, const std::string& projectDir,
                                     int* errors, int* warnings) {
    std::vector<Diag> diags;
    if (errors) *errors = 0;
    if (warnings) *warnings = 0;
    size_t start = 0;
    while (start <= raw.size()) {
        size_t      nl = raw.find('\n', start);
        std::string line = raw.substr(start, (nl == std::string::npos ? raw.size() : nl) - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string f;
        int         ln = 0, cl = 0, kind = 0;
        if (parseDiagnostic(line, &f, &ln, &cl, &kind)) {
            if (kind == 1 && errors) ++*errors;
            if (kind == 2 && warnings) ++*warnings;
            if (!f.empty() && f[0] != '/' && f.find(':') == std::string::npos)
                f = joinPath(projectDir, f);
            Diag d;
            d.severity = diagSeverity(kind);
            d.file = f;
            d.line = ln;
            d.col = cl;
            d.message = diagMessage(line);
            diags.push_back(std::move(d));
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return diags;
}

json diagToJson(const Diag& d) {
    json j;
    j["severity"] = d.severity;
    j["file"] = d.file;
    j["line"] = d.line;
    j["column"] = d.col;
    j["message"] = d.message;
    return j;
}

// 校验「这是一个能无头操作的工程」：目录存在、是新式工程（.easel/CMakeLists.txt，
// --new 建出来的都是这种）、cmake 找得到。三条里随便哪条不满足都直接说清楚原因。
// needCmake=false：这个命令压根不用编译（--clean）。**别让「没装 cmake」把清理也挡掉**——
// 构建目录坏掉、机器上没有 cmake，正好是最需要清理的场景之一。
bool resolveProject(const std::string& raw, std::string* dir, std::string* err,
                    bool needCmake = true) {
    std::string d = raw.empty() ? std::string() : absPath(raw);
    if (d.empty() || !isDirU8(d)) {
        *err = "找不到工程目录：" + raw;
        return false;
    }
    if (!existsU8(joinPath(d, ".easel/CMakeLists.txt"))) {
        *err = d + " 不像一个工程（没有 .easel/CMakeLists.txt，是不是没用 --new 建的？）";
        return false;
    }
    if (needCmake && toolchain().cmake.empty()) {
        *err = "找不到 cmake：装 CMake（brew install cmake / 工具箱里已自带）或把它加进 PATH";
        return false;
    }
    *dir = d;
    return true;
}

// 每次无头操作的完整输出（横幅 + 带时间戳的每一步 + 子进程原样输出）写到
// <工程>/.easel/last-build.log，覆盖式，只留最近一次——跟 GUI 的 flushLog() 是同一个
// 文件、同一套格式，出问题时不用分是从工作台点的还是脚本跑的。
void writeLog(const std::string& dir, const std::string& text) {
    if (dir.empty()) return;
    writeTextU8(joinPath(dir, ".easel/last-build.log"), text);
}

struct BuildOutcome {
    bool              ok = false;
    double            seconds = 0;
    int               errors = 0, warnings = 0;
    std::vector<Diag> diagnostics;
    std::string       raw;      // 纯子进程输出，--json 的 "raw" 字段用（不能被我们自己的行污染）
    std::string       pretty;   // 横幅 + 带时间戳的每一步 + 原样输出，文本模式 / 日志文件用
};

// 配置（如果还没配置过）+ 编 app 目标。跟 GUI 的 startConfigure()/startBuild() 拼
// 命令行的方式一样，只是同步等它跑完（runBlocking），不用每帧 pump。
BuildOutcome runBuild(const std::string& dir) {
    BuildOutcome     out;
    Stopwatch        sw;
    const Toolchain& tc = toolchain();
    std::string      build = buildDir(dir);
    std::string      raw;
    std::string      pretty = banner(dir);
    auto             stamp = [&](const std::string& t) { pretty += formatStamp(sw.s()) + t + "\n"; };
    auto             rawOut = [&](const std::string& t) {
        if (t.empty()) return;
        pretty += t;
        if (pretty.back() != '\n') pretty += "\n";
    };

    stamp("工程 " + dir);

    bool needConfigure = !buildConfigured(build);
    if (!needConfigure && easelDirStale(build, wb().easelDir)) {
        stamp("Easel 位置变了，重新配置构建目录");
        needConfigure = true;
    }
    if (needConfigure) {
        std::vector<std::string> argv = configureArgs(dir, build, "RelWithDebInfo");
        stamp("构建目录 " + build + "（" + (tc.ninja.empty() ? "Unix Makefiles" : "Ninja") +
              " · RelWithDebInfo）");
        for (const std::string& l : verboseEnvLines(dir)) stamp(l);
        stamp("$ " + Proc::describe(argv));
        std::string cfgRaw;
        int         cfgCode = -1;
        bool        ran = runBlocking(argv, dir, kTimeoutMs, &cfgRaw, &cfgCode, &tc.binDirs);
        raw += cfgRaw;
        rawOut(cfgRaw);
        if (!ran || cfgCode != 0) {
            stamp("配置失败（退出码 " + std::to_string(cfgCode) + "）。上面几行是原因。");
            if (tc.cmake.empty())
                stamp("cmake 没找到 —— 装 CMake（brew install cmake / 工具箱里已自带）或把它加进 PATH");
            std::string explain, command, fallback;
            if (detectMissingLinuxDeps(cfgRaw, &explain, &command, &fallback)) {
                stamp(explain);
                stamp(command);
                stamp(fallback);
            }
            out.raw = raw;
            out.pretty = pretty;
            out.seconds = sw.s();
            out.diagnostics = collectDiagnostics(raw, dir, &out.errors, &out.warnings);
            out.ok = false;
            return out;
        }
        stamp("配置完成");
        if (cfgRaw.find("改用源码编") != std::string::npos)
            stamp("已改用源码编（约 3 分钟）。要恢复秒级编译，在目标平台重新生成预编译包");
    }

    std::vector<std::string> argv{tc.cmake.empty() ? "cmake" : tc.cmake, "--build", build,
                                  "--target", "app", "--parallel"};
    stamp("$ " + Proc::describe(argv));
    std::string buildRaw;
    int         buildCode = -1;
    runBlocking(argv, dir, kTimeoutMs, &buildRaw, &buildCode, &tc.binDirs);
    raw += buildRaw;
    rawOut(buildRaw);

    out.raw = raw;
    out.seconds = sw.s();
    out.diagnostics = collectDiagnostics(raw, dir, &out.errors, &out.warnings);
    out.ok = (buildCode == 0);
    if (out.ok) {
        std::string exe = appExe(dir);
        long long   sz = 0;
        fileStamp(exe, nullptr, &sz);
        stamp("编译完成 -> " + exe + "（" + formatBytes(sz) + "）" +
              (out.warnings ? "，" + std::to_string(out.warnings) + " 个警告" : std::string()));
    } else if (out.errors == 0 && out.warnings == 0) {
        // 一个错误/警告都没有：--json 也拿不出诊断（diagnostics 数组会是空的），
        // 指望学生自己翻 --json 没意义。把构建这一步的原始输出结尾摆出来。
        stamp("编译失败，但没有具体的错误/警告可以点——这类失败通常不是代码本身的问题，"
              "是构建系统或者环境出了问题。原始输出最后几行：");
        for (const std::string& l : tailNonEmptyLines(buildRaw, 6)) rawOut(l);
    } else {
        stamp("编译失败：" + std::to_string(out.errors) + " 个错误、" + std::to_string(out.warnings) +
              " 个警告。用 --json 拿结构化诊断。");
    }
    out.pretty = pretty;
    return out;
}

json buildOutcomeJson(const std::string& action, const std::string& dir, const BuildOutcome& b) {
    json j;
    j["ok"] = b.ok;
    j["action"] = action;
    j["project"] = dir;
    j["seconds"] = b.seconds;
    j["errors"] = b.errors;
    j["warnings"] = b.warnings;
    json arr = json::array();
    for (const Diag& d : b.diagnostics) arr.push_back(diagToJson(d));
    j["diagnostics"] = arr;
    j["raw"] = b.raw;
    return j;
}

// 找不到工程 / 没有 .easel / 没有 cmake：三种情况统一走这一条出口。
int failEarly(const std::string& err, bool jsonOut) {
    if (jsonOut) {
        json j;
        j["ok"] = false;
        j["error"] = err;
        std::cout << j.dump(2) << std::endl;
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }
    return 1;
}

// easel --build <工程目录> [--json]：只配置 + 编译 app 目标，不运行。
int cmdBuild(const std::string& raw, bool jsonOut) {
    std::string dir, err;
    if (!resolveProject(raw, &dir, &err)) return failEarly(err, jsonOut);
    BuildOutcome b = runBuild(dir);
    writeLog(dir, b.pretty);
    if (jsonOut) {
        std::cout << buildOutcomeJson("build", dir, b).dump(2) << std::endl;
    } else {
        std::fwrite(b.pretty.data(), 1, b.pretty.size(), stdout);
    }
    return b.ok ? 0 : 1;
}

// easel --run <工程目录> [--args "..."] [--json]：编译后运行作品，等它退出。
// --args 把参数转给作品（比如 --args "--frames 30"，让作品跑够帧数自己退出，
// 不用人守着去 kill）。退出码 = 作品的退出码（编译失败时退出码 1，作品没机会跑）。
int cmdRun(const std::string& raw, bool jsonOut) {
    std::string dir, err;
    if (!resolveProject(raw, &dir, &err)) return failEarly(err, jsonOut);
    BuildOutcome b = runBuild(dir);
    if (!b.ok) {
        writeLog(dir, b.pretty);
        if (jsonOut) {
            std::cout << buildOutcomeJson("run", dir, b).dump(2) << std::endl;
        } else {
            std::fwrite(b.pretty.data(), 1, b.pretty.size(), stdout);
        }
        return 1;
    }

    std::string exe = appExe(dir);
    if (!existsU8(exe)) {
        writeLog(dir, b.pretty);
        return failEarly("没找到编出来的程序：" + exe, jsonOut);
    }

    std::vector<std::string> argv{exe};
    std::string               args = cli::args().str("args"), cur;
    for (size_t i = 0; i <= args.size(); ++i) {
        if (i == args.size() || args[i] == ' ') {
            if (!cur.empty()) argv.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(args[i]);
        }
    }

    std::string pretty = b.pretty;
    double      t0 = b.seconds;
    pretty += formatStamp(t0) + "$ " + Proc::describe(argv) + "\n";
    Stopwatch   runSw;
    std::string appRaw;
    int         appCode = -1;
    runBlocking(argv, dir, kTimeoutMs, &appRaw, &appCode, &toolchain().binDirs);
    if (!appRaw.empty()) {
        pretty += appRaw;
        if (pretty.back() != '\n') pretty += "\n";
    }
    double t1 = t0 + runSw.s();
    if (appCode == 0) {
        pretty += formatStamp(t1) + "作品退出，退出码 0\n";
    } else if (appCode >= 128) {
        pretty += formatStamp(t1) + "作品被信号 " + std::to_string(appCode - 128) +
                  " 打断 —— 多半是越界或空指针。用 --json 拿结构化诊断，或者用 VS Code 的 F5 调试。\n";
    } else {
        pretty += formatStamp(t1) + "作品退出，退出码 " + std::to_string(appCode) + "\n";
    }
    bool ok = (appCode == 0);
    writeLog(dir, pretty);

    if (jsonOut) {
        json j = buildOutcomeJson("run", dir, b);
        j["ok"] = ok;
        j["exitCode"] = appCode;
        j["stdout"] = appRaw;
        std::cout << j.dump(2) << std::endl;
    } else {
        std::fwrite(pretty.data(), 1, pretty.size(), stdout);
    }
    return appCode;
}

// easel --package <工程目录> [--json]：Release 构建 + 收进 dist/<名字>-release/。
int cmdPackage(const std::string& raw, bool jsonOut) {
    std::string dir, err;
    if (!resolveProject(raw, &dir, &err)) return failEarly(err, jsonOut);

    Stopwatch        sw;
    const Toolchain& tc = toolchain();
    std::string      rel = releaseDir(dir);
    std::string      raw2, cfgRaw, buildRaw;
    std::string      pretty = banner(dir);
    auto             stamp = [&](const std::string& t) { pretty += formatStamp(sw.s()) + t + "\n"; };
    auto             rawOut = [&](const std::string& t) {
        if (t.empty()) return;
        pretty += t;
        if (pretty.back() != '\n') pretty += "\n";
    };

    stamp("工程 " + dir);
    bool alreadyConfigured = buildConfigured(rel);
    if (alreadyConfigured && easelDirStale(rel, wb().easelDir)) {
        stamp("Easel 位置变了，重新配置构建目录");
        alreadyConfigured = false;
    }
    int cfgCode = alreadyConfigured ? 0 : -1;
    if (cfgCode != 0) {
        std::vector<std::string> argv = configureArgs(dir, rel, "Release");
        stamp("构建目录 " + rel + "（" + (tc.ninja.empty() ? "Unix Makefiles" : "Ninja") + " · Release）");
        for (const std::string& l : verboseEnvLines(dir)) stamp(l);
        stamp("$ " + Proc::describe(argv));
        runBlocking(argv, dir, kTimeoutMs, &cfgRaw, &cfgCode, &tc.binDirs);
        raw2 += cfgRaw;
        rawOut(cfgRaw);
        if (cfgCode == 0) {
            stamp("配置完成");
            if (cfgRaw.find("改用源码编") != std::string::npos)
                stamp("已改用源码编（约 3 分钟）。要恢复秒级编译，在目标平台重新生成预编译包");
        } else {
            stamp("配置失败（退出码 " + std::to_string(cfgCode) + "）。上面几行是原因。");
            if (tc.cmake.empty())
                stamp("cmake 没找到 —— 装 CMake（brew install cmake / 工具箱里已自带）或把它加进 PATH");
            std::string explain, command, fallback;
            if (detectMissingLinuxDeps(cfgRaw, &explain, &command, &fallback)) {
                stamp(explain);
                stamp(command);
                stamp(fallback);
            }
        }
    }

    int buildCode = -1;
    if (cfgCode == 0) {
        std::vector<std::string> argv{tc.cmake.empty() ? "cmake" : tc.cmake, "--build", rel,
                                      "--config", "Release", "--target", "app", "--parallel"};
        stamp("$ " + Proc::describe(argv));
        runBlocking(argv, dir, kTimeoutMs, &buildRaw, &buildCode, &tc.binDirs);
        raw2 += buildRaw;
        rawOut(buildRaw);
    }

    BuildOutcome b;
    b.raw = raw2;
    b.seconds = sw.s();
    b.diagnostics = collectDiagnostics(raw2, dir, &b.errors, &b.warnings);
    b.ok = (cfgCode == 0 && buildCode == 0);

    if (b.ok)
        stamp("编译完成" +
              (b.warnings ? "，" + std::to_string(b.warnings) + " 个警告" : std::string()));
    else if (cfgCode == 0 && b.errors == 0 && b.warnings == 0) {
        stamp("编译失败，但没有具体的错误/警告可以点——这类失败通常不是代码本身的问题，"
              "是构建系统或者环境出了问题。原始输出最后几行：");
        for (const std::string& l : tailNonEmptyLines(buildRaw, 6)) rawOut(l);
    } else if (cfgCode == 0) {
        stamp("编译失败：" + std::to_string(b.errors) + " 个错误、" + std::to_string(b.warnings) +
              " 个警告。用 --json 拿结构化诊断。");
    }

    std::string output, packErr;
    long long   bytes = 0;
    if (b.ok) {
        output = packageInto(dir, &packErr, &bytes);
        if (output.empty()) b.ok = false;
    }
    if (!output.empty())
        stamp("生成完成 -> " + output + "（" + formatBytes(bytes) + "）");
    else if (!packErr.empty())
        stamp(packErr);

    b.pretty = pretty;
    writeLog(dir, pretty);

    if (jsonOut) {
        json j = buildOutcomeJson("package", dir, b);
        if (!output.empty()) j["output"] = output;
        if (!packErr.empty()) j["error"] = packErr;
        std::cout << j.dump(2) << std::endl;
    } else {
        std::fwrite(pretty.data(), 1, pretty.size(), stdout);
    }
    return b.ok ? 0 : 1;
}

// easel --export <工程目录> --out <目录> [--json]：导出可独立编译的完整工程。
// --out 留空就用 exportProject() 自己的默认值（<工程目录>/dist/<名字>）。
int cmdExport(const std::string& raw, bool jsonOut) {
    std::string dir, err;
    if (!resolveProject(raw, &dir, &err)) return failEarly(err, jsonOut);

    ExportOptions o;
    o.projectDir = dir;
    o.easelDir = wb().easelDir;
    o.name = baseName(dir);
    o.outDir = cli::args().str("out");
    std::string outDirShown =
        o.outDir.empty() ? joinPath(joinPath(dir, "dist"), o.name) : absPath(o.outDir);

    std::string pretty = banner(dir);
    Stopwatch   sw;
    auto        stamp = [&](const std::string& t) { pretty += formatStamp(sw.s()) + t + "\n"; };
    stamp("源工程 " + dir);
    stamp("导出到 " + outDirShown);
    for (const std::string& l : verboseEnvLines(dir)) stamp(l);

    ExportReport rep = exportProject(o);
    for (const std::string& c : rep.checks) pretty += c + "\n";
    if (rep.ok)
        stamp("导出完成 -> " + rep.outDir + "（" + std::to_string(rep.files) + " 个文件，" +
              formatBytes(rep.bytes) + "）");
    else
        stamp("导出失败：" + rep.error);

    writeLog(dir, pretty);

    if (jsonOut) {
        json j;
        j["ok"] = rep.ok;
        j["action"] = "export";
        j["project"] = dir;
        j["output"] = rep.outDir;
        j["files"] = rep.files;
        j["bytes"] = rep.bytes;
        json checks = json::array();
        for (const std::string& c : rep.checks) checks.push_back(c);
        j["checks"] = checks;
        if (!rep.ok) j["error"] = rep.error;
        std::cout << j.dump(2) << std::endl;
    } else {
        std::fwrite(pretty.data(), 1, pretty.size(), stdout);
    }
    return rep.ok ? 0 : 1;
}

// easel --clean <工程目录> [--json]：删掉两个构建目录，腾出空间。和界面上的「清理」同一份
// 实现（cleanProject()），删什么不删什么见 docs/ui-design.md 6.3 —— 尤其是 dist/ 不动。
// 幂等：目录本来就不存在也算成功（脚本敢连着调两次）。清理不需要 cmake。
int cmdClean(const std::string& raw, bool jsonOut) {
    std::string dir, err;
    if (!resolveProject(raw, &dir, &err, /*needCmake=*/false)) return failEarly(err, jsonOut);

    Stopwatch   sw;
    std::string pretty = banner(dir);
    auto        stamp = [&](const std::string& t) { pretty += formatStamp(sw.s()) + t + "\n"; };
    stamp("工程 " + dir);
    stamp("清理构建产物（不动 dist/，也不动你的代码）");
    CleanReport rep = cleanProject(dir);
    for (const std::string& d : rep.removed) stamp("删掉 " + d);
    if (!rep.ok)
        stamp(rep.error);
    else if (rep.removed.empty())
        stamp("没有构建产物可清理（两个构建目录都不在）");
    else
        stamp("清理完成，腾出 " + formatBytes(rep.bytes));
    writeLog(dir, pretty);

    if (jsonOut) {
        json j;
        j["ok"] = rep.ok;
        j["action"] = "clean";
        j["project"] = dir;
        json arr = json::array();
        for (const std::string& d : rep.removed) arr.push_back(d);
        j["removed"] = arr;
        j["bytes"] = rep.bytes;
        if (!rep.ok) j["error"] = rep.error;
        std::cout << j.dump(2) << std::endl;
    } else {
        std::fwrite(pretty.data(), 1, pretty.size(), stdout);
    }
    return rep.ok ? 0 : 1;
}

}  // namespace headless

// ------------------------------------------------------------------ 界面
void drawNewProjectPopup(float dpi) {
    WB& w = wb();
    if (!ImGui::BeginPopupModal("新建工程", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextWrapped("从模板生成一个新工程：界面、逻辑、示例数据、测试都给好了，改就是了。");
    ImGui::Spacing();
    ImGui::InputText("作品名", w.newName, sizeof w.newName);
    ImGui::TextDisabled("只能用英文字母、数字、下划线、连字符（编译器对中文路径支持不好）");
    ImGui::InputTextWithHint("放在哪", "选一个目录", w.newParent, sizeof w.newParent);
    ImGui::SameLine();
    if (ImGui::Button("选目录…")) {
        std::string d = file::folder(w.newParent[0] ? w.newParent : nullptr);
        if (!d.empty()) std::snprintf(w.newParent, sizeof w.newParent, "%s", d.c_str());
    }
    ImGui::TextDisabled("这个目录的路径也不能含中文");
    ImGui::Spacing();

    std::vector<std::string> options{"空白", "算法骨架（读数据、逐帧回放、收敛曲线）"};
    for (const std::string& e : w.exampleNames) options.push_back("示例：" + e);
    if (w.newSkeleton < 0 || w.newSkeleton >= (int)options.size()) w.newSkeleton = 0;
    if (ImGui::BeginCombo("骨架", options[w.newSkeleton].c_str())) {
        for (int i = 0; i < (int)options.size(); ++i) {
            bool sel = (i == w.newSkeleton);
            if (ImGui::Selectable(options[i].c_str(), sel)) w.newSkeleton = i;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "空白：一个文件，画个 Hello，从零开始写\n"
            "算法骨架：读数据文件、逐帧回放、收敛曲线、导出用例\n"
            "示例：Easel 自带的可运行例子，拿来改");

    ImGui::BeginDisabled(w.newSkeleton != 1);   // 测试文件只对算法骨架有意义
    ImGui::Checkbox("顺便带一个测试文件 tests/test_solver.cpp", &w.newTests);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("不勾也没关系：以后新建这个文件，下次编译会自动带上");
    ImGui::Spacing();
    ImGui::TextDisabled("会建出：%s", joinPath(w.newParent, w.newName).c_str());
    ImGui::Spacing();
    if (ImGui::Button("创建", ImVec2(120 * dpi, 0))) {
        NewProjectOptions o;
        o.easelDir = w.easelDir;
        o.parentDir = w.newParent;
        o.name = w.newName;
        o.withTests = w.newTests;
        if (w.newSkeleton == 1)
            o.fullSkeleton = true;
        else if (w.newSkeleton >= 2)
            o.exampleDir = joinPath(joinPath(w.easelDir, "examples"), w.exampleNames[w.newSkeleton - 2]);
        NewProjectReport r = createProject(o);
        if (r.ok) {
            useProject(r.dir);
            w.out.clear();
            say("新工程建好了：" + r.dir + "（" + std::to_string(r.files) + " 个文件）");
            say("界面写在 src/app.cpp，算法写在 src/solver.cpp。改完回来点「运行」。");
            ImGui::CloseCurrentPopup();
        } else {
            addOut(r.error, 1);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("取消", ImVec2(100 * dpi, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- 状态机
// 状态 = 有没有工程 × stage × 上一次操作的结果。六个状态，界面没有第七种长相
// （docs/ui-design.md 第 2 节）。
UiState uiState() {
    const WB& w = wb();
    if (w.projectDir.empty()) return StEmpty;
    if (w.stage == Running) return StRunning;
    if (w.pendingExportProject || w.stage == Configuring || w.stage == Building || w.proc.running())
        return StBusy;
    if (w.lastResult == ResFail) return StFail;
    if (w.lastResult == ResOk) return StOk;
    return StFresh;
}

bool busyState(UiState st) { return st == StBusy || st == StRunning; }

// 四档字号，只能通过这两个函数用（docs/ui-design.md 5.3）
void pushTier(float tier) { ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * tier); }
void popTier() { ImGui::PopFont(); }

// ---------------------------------------------------------------- 动作
void stopNow() {
    WB& w = wb();
    if (!w.proc.running()) return;
    w.proc.kill();
    say("已停止");
    w.stage = Idle;
    w.status = "就绪";
    w.pendingRun = false;
    w.busyText.clear();
    setSummary("已停止", 0);
    flushLog();
}

void openProjectDialog() {
    WB&         w = wb();
    std::string d = file::folder(w.projectDir.empty() ? nullptr : w.projectDir.c_str());
    if (d.empty()) return;
    if (existsU8(joinPath(d, "src/app.cpp")))
        useProject(d);
    else
        addOut(d + " 不像一个作品工程（里面没有 src/app.cpp）", 1);
}

void copyAllOutput() {
    std::string all;
    for (const OutLine& o : wb().out) all += o.text + "\n";
    ImGui::SetClipboardText(all.c_str());
}

// ---------------------------------------------------------------- 两种模式
// 紧凑模式 = 把日志区收起来，窗口高度贴着剩下四段的内容（docs/ui-design.md 4.2）。
// 自动改窗口大小只发生在「切模式」「进 / 出失败状态」这两个时刻，而且是一次性的；
// 用户之后自己怎么拖都不管（fitCompactHeight 里用 lastSetH 判断有没有被插手过）。
void setCompact(bool on, float dpi);

void fitCompactHeight(float dpi, float neededPx) {
    WB&         w = wb();
    GLFWwindow* win = wbWindow();
    if (!win || !w.compact || w.fitFrames <= 0) return;
    int cw = 0, ch = 0;
    glfwGetWindowSize(win, &cw, &ch);
    if (w.lastSetH && std::abs(ch - w.lastSetH) > 2) {   // 用户自己拖过高度：别再跟他抢
        w.fitFrames = 0;
        return;
    }
    int want = (int)(neededPx + 0.5f);
    int maxH = (int)(M::kGrowMaxH * dpi), minH = (int)(M::kMinCompactH * dpi);
    if (want > maxH) want = maxH;
    if (want < minH) want = minH;
    if (std::abs(want - ch) > 2) {
        glfwSetWindowSize(win, cw, want);
        w.lastSetH = want;
    }
    --w.fitFrames;
}

void setCompact(bool on, float dpi) {
    WB& w = wb();
    if (w.compact == on) return;
    GLFWwindow* win = wbWindow();
    if (win && !w.compact) {   // 离开完整模式之前记住它的尺寸，回来要还原
        int cw = 0, ch = 0;
        glfwGetWindowSize(win, &cw, &ch);
        w.fullW = (int)(cw / dpi);
        w.fullH = (int)(ch / dpi);
    }
    w.compact = on;
    w.grown = false;
    applySizeLimits(dpi);
    if (!win) return;
    if (on) {
        // 先给一个估计值，下一帧按实际内容高度贴合（fitCompactHeight）
        int est = (int)((M::kProjectH + M::kActionH + M::kSummaryH + 60.f) * dpi);
        glfwSetWindowSize(win, (int)(M::kCompactW * dpi), est);
        w.lastSetH = est;
        w.fitFrames = 3;
    } else {
        glfwSetWindowSize(win, (int)(w.fullW * dpi), (int)(w.fullH * dpi));
        w.lastSetH = 0;
        w.fitFrames = 0;
    }
}

// 窗口位置 / 完整模式尺寸每帧记在内存里，退出时写一次盘（别每帧写盘）
void trackGeometry(float dpi) {
    WB&         w = wb();
    GLFWwindow* win = wbWindow();
    if (!win) return;
    int x = 0, y = 0;
    glfwGetWindowPos(win, &x, &y);
    w.winX = x;
    w.winY = y;
    if (!w.compact) {
        int cw = 0, ch = 0;
        glfwGetWindowSize(win, &cw, &ch);
        if (cw > 0 && ch > 0) {
            w.fullW = (int)(cw / dpi);
            w.fullH = (int)(ch / dpi);
        }
    }
}

// 帮助 → 环境自检：把 --doctor 的内容打进日志区。紧凑模式下日志是收起来的，
// 那就顺手展开——否则点下去界面上什么都不会发生。
void runDoctor(float dpi) {
    WB& w = wb();
    if (!App::instance()) return;
    beginLog();
    sayTS("环境自检（命令行等价物：easel --doctor）");
    std::string text = App::instance()->doctor();
    size_t      start = 0;
    while (start < text.size()) {
        size_t      nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!line.empty()) addOut(line, 0);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    setCompact(false, dpi);
    flushLog();
}

// ---------------------------------------------------------------- 菜单栏
// 「一个作品用一次」的操作全在这儿（docs/ui-design.md 第 3 节）。主操作区只留运行 / 停止。
void drawMenuBar(float dpi) {
    WB&    w = wb();
    UiState st = uiState();
    bool   busy = busyState(st);
    bool   hasProj = !w.projectDir.empty();
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("文件")) {
        if (hasProj && w.cleanBytes < 0) w.cleanBytes = cleanableBytes(w.projectDir);
        if (ImGui::MenuItem("新建工程…")) {
            if (!w.newParent[0])
                std::snprintf(w.newParent, sizeof w.newParent, "%s", defaultProjectsDir().c_str());
            w.openNewProject = true;
        }
        if (ImGui::MenuItem("打开工程…")) openProjectDialog();
        if (ImGui::BeginMenu("最近打开", !w.recent.empty())) {
            for (const std::string& d : w.recent)
                if (ImGui::MenuItem(d.c_str())) useProject(d);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("导出应用程序…", nullptr, false, hasProj && !busy)) w.openExportApp = true;
        if (ImGui::MenuItem("导出工程…", nullptr, false, hasProj && !busy)) {
            std::snprintf(w.exportOutDir, sizeof w.exportOutDir, "%s",
                          joinPath(joinPath(w.projectDir, "dist"), baseName(w.projectDir)).c_str());
            w.openExportProject = true;
        }
        ImGui::Separator();
        // 清理：不弹确认框（和 VS / Xcode / IntelliJ 一致），但点之前就把删什么、多大说清楚
        if (ImGui::MenuItem("清理", nullptr, false, hasProj && !busy)) doClean();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            std::string tip = "删掉这个工程的两个构建目录（编译产物，可以重新生成）";
            if (hasProj && w.cleanBytes > 0) tip += "，现在约 " + formatBytes(w.cleanBytes);
            tip += "。\n不会动 dist/ 里导出的东西，也不会动你的代码。";
            ImGui::SetTooltip("%s", tip.c_str());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("复制工程路径", nullptr, false, hasProj)) {
            ImGui::SetClipboardText(w.projectDir.c_str());
            say("工程路径已复制：" + w.projectDir);
        }
        if (ImGui::MenuItem("复制输出", nullptr, false, !w.out.empty())) copyAllOutput();
        if (ImGui::MenuItem("打开日志文件", nullptr, false, hasProj))
            openLogFile(joinPath(w.projectDir, ".easel/last-build.log"));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("这次操作的完整输出存在 .easel/last-build.log 里");
        ImGui::Separator();
        if (ImGui::MenuItem("退出") && App::instance()) App::instance()->quit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("设置")) {
        if (ImGui::MenuItem("运行参数…", nullptr, false, !busy)) w.openRunArgs = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("原样传给作品的命令行参数，相当于 easel --run <工程> --args \"…\"");
        ImGui::Separator();
        if (ImGui::MenuItem("详细输出", nullptr, w.verboseUI)) {
            w.verboseUI = !w.verboseUI;
            internal::setVerbose(w.verboseUI);
            saveConfig();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("下一次操作会多打一截排障细节：完整 PATH、子进程工作目录、\n"
                              "easel-prebuilt.json / VERSION.json 的原文。命令行等价于 --verbose");
        if (ImGui::MenuItem("紧凑模式", nullptr, w.compact)) setCompact(!w.compact, dpi);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("把输出区收起来，只留工程、运行 / 停止和结论那一行");
        bool canTop = wbWindow() != nullptr;
        if (ImGui::MenuItem("始终置顶", nullptr, w.alwaysOnTop, canTop)) {
            w.alwaysOnTop = !w.alwaysOnTop;
            applyAlwaysOnTop();
            saveConfig();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("帮助")) {
        if (ImGui::MenuItem("环境自检", nullptr, false, !busy)) runDoctor(dpi);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("把环境情况打进输出区（命令行等价物：easel --doctor）");
        if (ImGui::MenuItem("关于 Easel")) w.openAbout = true;
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

// ---------------------------------------------------------------- 工程栏
// 「现在是哪个工程」。路径弱化（小一号 + 次要色），它是用来核对「我是不是打开错了」的，
// 不是主角；点一下复制。右边只有两个东西：始终置顶、收起 / 展开输出区。
void drawProjectBar(const Theme& th, float dpi) {
    WB&   w = wb();
    float y0 = ImGui::GetCursorPosY();

    // 右侧先量宽度，再右对齐摆
    float topW = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                 ImGui::CalcTextSize("始终置顶").x;
    const char* foldLabel = w.compact ? "展开" : "收起";
    float       foldW = ImGui::CalcTextSize(foldLabel).x + ImGui::GetStyle().FramePadding.x * 2.f;

    ImGui::BeginGroup();
    pushTier(M::kT1);
    ImGui::TextUnformatted(w.projectDir.empty() ? "还没有打开工程" : baseName(w.projectDir).c_str());
    popTier();
    if (!w.projectDir.empty()) {
        pushTier(M::kT3);
        ImGui::PushStyleColor(ImGuiCol_Text, iv4(th.muted));
        std::string line = w.projectDir;
        if (w.runArgs[0]) line += "   参数：" + std::string(w.runArgs);
        if (ImGui::Selectable(line.c_str(), false, 0,
                              ImVec2(ImGui::CalcTextSize(line.c_str()).x, 0))) {
            ImGui::SetClipboardText(w.projectDir.c_str());
            say("工程路径已复制：" + w.projectDir);
        }
        ImGui::PopStyleColor();
        popTier();
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("点一下复制工程路径");
        }
    }
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::SetCursorPosY(y0);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x -
                         topW - M::kGap * dpi - foldW);
    ImGui::BeginDisabled(wbWindow() == nullptr);
    if (ImGui::Checkbox("始终置顶", &w.alwaysOnTop)) {
        applyAlwaysOnTop();
        saveConfig();
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, M::kGap * dpi);
    // 折叠箭头用 ImGui 自己画的（字体子集里没有 ⌃ 这类符号，见 docs/ui-design.md 第 0 节），
    // 这里用文字按钮，和「设置 → 紧凑模式」是同一个状态，不是两个开关。
    if (ImGui::SmallButton(foldLabel)) setCompact(!w.compact, dpi);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(w.compact ? "展开输出区（退出紧凑模式）" : "收起输出区（紧凑模式）");

    ImGui::SetCursorPosY(y0 + M::kProjectH * dpi);
    ImGui::Separator();
}

// ---------------------------------------------------------------- 主操作区
// 只有运行 / 停止两个按钮——循环里高频用的就这两个（docs/ui-design.md 第 3 节）。
// 空状态是唯一的例外：那时候「运行」无从下手，这一屏里真正高频的是新建 / 打开。
void drawActionArea(const Theme& th, float dpi) {
    WB&     w = wb();
    UiState st = uiState();
    float   y0 = ImGui::GetCursorPosY();

    if (st == StEmpty) {
        ImGui::SetCursorPosY(y0 + M::kGapTight * dpi);
        ImGui::TextUnformatted("先新建一个工程，或者打开一个已有的");
        ImGui::Spacing();
        if (ImGui::Button("新建工程…", ImVec2(M::kBtnRunW * dpi, M::kBtnH * dpi))) {
            if (!w.newParent[0])
                std::snprintf(w.newParent, sizeof w.newParent, "%s", defaultProjectsDir().c_str());
            w.openNewProject = true;
        }
        ImGui::SameLine(0, M::kGapSection * dpi);
        if (ImGui::Button("打开工程…", ImVec2(M::kBtnRunW * dpi, M::kBtnH * dpi)))
            openProjectDialog();
        if (!w.recent.empty()) {
            ImGui::Spacing();
            pushTier(M::kT3);
            ImGui::PushStyleColor(ImGuiCol_Text, iv4(th.muted));
            ImGui::TextUnformatted("最近打开：");
            ImGui::PopStyleColor();
            popTier();
            for (size_t i = 0; i < w.recent.size() && i < (size_t)M::kRecentInEmpty; ++i) {
                ImGui::SameLine(0, M::kGapTight * dpi);
                pushTier(M::kT3);
                if (ImGui::SmallButton(baseName(w.recent[i]).c_str())) useProject(w.recent[i]);
                popTier();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", w.recent[i].c_str());
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        return;
    }

    // 按钮在这一段里垂直居中
    ImGui::SetCursorPosY(y0 + (M::kActionH - M::kBtnH) * 0.5f * dpi);
    ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x);

    // 运行：配置 / 编译中置灰（同一个构建目录并发两个 cmake 会把它搞坏）；作品运行中不灰，
    // 按下去就是重跑（理由见 docs/ui-design.md 2.2）。
    bool runEnabled = (st != StBusy);
    ImGui::BeginDisabled(!runEnabled);
    // 全界面唯一一处用主色填充的元素（docs/ui-design.md 5.5）。置灰时**不涂主色**——
    // 只靠透明度区分「能不能按」太弱，一个绿按钮半透明了还是绿的。
    if (runEnabled) ImGui::PushStyleColor(ImGuiCol_Button, iv4(th.accent));
    pushTier(M::kT2b);
    if (ImGui::Button("运行   F5", ImVec2(M::kBtnRunW * dpi, M::kBtnH * dpi))) runOrRerun();
    popTier();
    if (runEnabled) ImGui::PopStyleColor();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(st == StRunning ? "再跑一次：先停掉正在跑的作品，重新编译运行"
                                          : "编译你的作品并把它跑起来（快捷键 F5）");

    ImGui::SameLine(0, M::kGapSection * dpi);
    ImGui::BeginDisabled(!busyState(st));
    if (ImGui::Button("停止", ImVec2(M::kBtnStopW * dpi, M::kBtnH * dpi))) stopNow();
    ImGui::EndDisabled();

    ImGui::SetCursorPosY(y0 + M::kActionH * dpi);
    ImGui::Separator();
}

// ---------------------------------------------------------------- 摘要行
// 单行，不换行、不长高。放不下就从右往左丢次要片段（结论永远留着）。
std::string fitSummary(const std::string& text, float avail) {
    if (ImGui::CalcTextSize(text.c_str()).x <= avail) return text;
    // 片段是用 " · " 串起来的，从右往左丢
    std::vector<std::string> parts;
    const std::string        sep = " · ";
    size_t                   start = 0;
    for (;;) {
        size_t p = text.find(sep, start);
        if (p == std::string::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, p - start));
        start = p + sep.size();
    }
    while (parts.size() > 1) {
        parts.pop_back();
        std::string joined = parts[0];
        for (size_t i = 1; i < parts.size(); ++i) joined += sep + parts[i];
        if (ImGui::CalcTextSize(joined.c_str()).x <= avail) return joined;
    }
    return parts.empty() ? text : parts[0];
}

void drawSummary(const Theme& th, float dpi) {
    WB&     w = wb();
    UiState st = uiState();
    if (st == StEmpty) return;   // 空状态没有结论可说，这一段整个不画（也不留空行）

    std::string text;
    int         kind = 0;
    if (busyState(st) && !w.busyText.empty()) {
        text = w.busyText + " " + secText(ImGui::GetTime() - w.busyFrom);
    } else if (!w.sumText.empty()) {
        text = w.sumText;
        kind = w.sumKind;
    } else {
        text = "还没运行过 · 按 F5 或点运行";
    }

    float y0 = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(y0 + (M::kSummaryH * dpi - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x);
    ImVec4 col = kind == 1   ? iv4(th.bad)
                 : kind == 2 ? iv4(th.good)
                 : kind == 3 ? iv4(th.warn)
                             : iv4(th.muted);
    float  avail = ImGui::GetContentRegionAvail().x;
    std::string shown = fitSummary(text, avail);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    if (!w.sumFile.empty() && w.sumLine > 0) {
        // 失败时摘要行能点：跳到第一条错误（和点日志里那一行等价）
        if (ImGui::Selectable(shown.c_str(), false, 0,
                              ImVec2(ImGui::CalcTextSize(shown.c_str()).x, 0)))
            jumpTo(w.sumFile, w.sumLine, w.sumCol);
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("点一下跳到出错的那一行");
        }
    } else {
        ImGui::TextUnformatted(shown.c_str());
    }
    ImGui::PopStyleColor();

    ImGui::SetCursorPosY(y0 + M::kSummaryH * dpi);
    ImGui::Separator();
}

// ---------------------------------------------------------------- 错误速览（紧凑模式专用）
// 紧凑模式 + 失败：窗口自动长高，把前几条错误露出来（成功不长高，否则每次编译窗口都跳一下）。
void drawErrorPeek(const Theme& th) {
    WB& w = wb();
    int shown = 0;
    ImGui::PushFont(monoFont(), ImGui::GetStyle().FontSizeBase * M::kT3);
    for (size_t i = 0; i < w.out.size() && shown < M::kGrowLines; ++i) {
        const OutLine& o = w.out[i];
        if (o.kind != 1) continue;
        ++shown;
        ImGui::PushStyleColor(ImGuiCol_Text, iv4(th.bad));
        if (!o.file.empty() && o.line > 0) {
            ImGui::PushID((int)i);
            if (ImGui::Selectable(o.text.c_str())) jumpTo(o.file, o.line, o.col);
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PopID();
        } else {
            ImGui::TextUnformatted(o.text.c_str());
        }
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();
}

// ---------------------------------------------------------------- 输出区
// 证据在这儿：编译器 / cmake / 作品的原样输出，一行不少。结论归摘要行。
void drawLogPane(const Theme& th, float dpi) {
    WB& w = wb();
    ImGui::TextUnformatted("输出");
    ImGui::SameLine(0, M::kGapSection * dpi);
    if (ImGui::Checkbox("详细输出", &w.verboseUI)) {
        internal::setVerbose(w.verboseUI);
        saveConfig();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("勾上之后下一次操作会多打一截排障细节：完整 PATH、子进程工作目录、\n"
                          "easel-prebuilt.json / VERSION.json 的原文。命令行等价于 --verbose");
    ImGui::SameLine(0, M::kGap * dpi);
    if (ImGui::SmallButton("清空输出")) {
        w.out.clear();
        w.errors = w.warnings = 0;
        w.firstError = -1;
        w.scrollTo = -1;
    }
    const char* foldLabel = "收起";
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x -
                         ImGui::CalcTextSize(foldLabel).x - ImGui::GetStyle().FramePadding.x * 2.f);
    if (ImGui::SmallButton(foldLabel)) setCompact(true, dpi);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("收起输出区（紧凑模式）");

    ImGui::BeginChild("##out", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    // 用户自己往上滚过之后就别再抢滚动条，直到下一次操作开始（beginLog 会重置）
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.f) w.stickBottom = false;
    ImGui::PushFont(monoFont(), ImGui::GetStyle().FontSizeBase * M::kT3);
    for (size_t i = 0; i < w.out.size(); ++i) {
        const OutLine& o = w.out[i];
        ImVec4         c = o.kind == 1   ? iv4(th.bad)
                           : o.kind == 2 ? iv4(th.warn)
                           // 我们自己说的话是旁白，不是重点（主色只留给「运行」按钮）
                           : o.kind == 3 ? iv4(th.muted)
                                         : iv4(th.fg);
        ImGui::PushStyleColor(ImGuiCol_Text, c);
        if (!o.file.empty() && o.line > 0) {
            ImGui::PushID((int)i);
            if (ImGui::Selectable(o.text.c_str())) jumpTo(o.file, o.line, o.col);
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PopID();
        } else {
            ImGui::TextUnformatted(o.text.c_str());
        }
        ImGui::PopStyleColor();
        // 失败时把视口滚到第一条错误那一行（底部只是重复摘要行，没用）
        if (w.scrollTo == (int)i) {
            ImGui::SetScrollHereY(0.25f);
            w.scrollTo = -1;
        }
    }
    ImGui::PopFont();
    if (w.stickBottom && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.f)
        ImGui::SetScrollHereY(1.f);
    ImGui::EndChild();
}

// ---------------------------------------------------------------- 对话框
// 「导出应用程序…」的确认框：省略号对应的就是它。一屏、一句话、一个确认一个取消，
// 不做成选项清单（docs/ui-design.md 6.2）。
void drawExportAppPopup(float dpi) {
    WB& w = wb();
    if (!ImGui::BeginPopupModal("导出应用程序", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    std::string dest = joinPath(joinPath(w.projectDir, "dist"), baseName(w.projectDir) + "-release");
    ImGui::TextWrapped("编一个优化过的版本，连同 assets / data 放进下面这个目录，"
                       "拷给别人双击就能运行。");
    ImGui::Spacing();
    ImGui::TextDisabled("%s", dest.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("要重新编译一遍，大约几十秒。");
    ImGui::Spacing();
    if (ImGui::Button("导出", ImVec2(120 * dpi, 0))) {
        ImGui::CloseCurrentPopup();
        startPackage();
    }
    ImGui::SameLine();
    if (ImGui::Button("取消", ImVec2(100 * dpi, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void drawExportProjectPopup(float dpi) {
    WB& w = wb();
    if (!ImGui::BeginPopupModal("导出工程", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextWrapped("导出一个完整工程：不装 Easel、不联网也能从头编出来（适合交作业、发给别人改）。");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(420 * dpi);
    ImGui::InputText("导出到", w.exportOutDir, sizeof w.exportOutDir);
    ImGui::SameLine();
    if (ImGui::Button("选目录…")) {
        std::string d = file::folder(w.exportOutDir[0] ? w.exportOutDir : nullptr);
        if (!d.empty()) std::snprintf(w.exportOutDir, sizeof w.exportOutDir, "%s", d.c_str());
    }
    ImGui::Spacing();
    ImGui::TextDisabled("几十 MB，界面会卡一下。");
    ImGui::Spacing();
    if (ImGui::Button("导出", ImVec2(120 * dpi, 0))) {
        ImGui::CloseCurrentPopup();
        beginExportProject();
    }
    ImGui::SameLine();
    if (ImGui::Button("取消", ImVec2(100 * dpi, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void drawRunArgsPopup(float dpi) {
    WB& w = wb();
    if (!ImGui::BeginPopupModal("运行参数", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextWrapped("原样传给你的作品的命令行参数，留空就是不传。");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(420 * dpi);
    ImGui::InputTextWithHint("##args", "例如 --open data/example.json --solve", w.runArgs,
                             sizeof w.runArgs);
    ImGui::Spacing();
    ImGui::TextDisabled("只对这次打开有效，不会记到下次（命令行等价物：--run <工程> --args \"…\"）");
    ImGui::Spacing();
    if (ImGui::Button("确定", ImVec2(120 * dpi, 0))) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (ImGui::Button("清空", ImVec2(100 * dpi, 0))) w.runArgs[0] = 0;
    ImGui::EndPopup();
}

void drawAboutPopup(float dpi) {
    if (!ImGui::BeginPopupModal("关于 Easel", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const WB&   w = wb();
    std::string text = versionLine() + "\n程序 " + exePath() + "\nEasel 源码 " +
                       (w.easelDir.empty() ? std::string("(没找到)") : w.easelDir);
    ImGui::TextUnformatted(text.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("更详细的环境情况：帮助菜单里的「环境自检」");
    ImGui::Spacing();
    if (ImGui::Button("复制", ImVec2(100 * dpi, 0))) ImGui::SetClipboardText(text.c_str());
    ImGui::SameLine();
    if (ImGui::Button("关闭", ImVec2(100 * dpi, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- 主界面
// 三段式（工程 → 运行 → 结论）+ 输出区。紧凑模式就是把最后一段收起来，骨架是同一套。
void draw(const Rect& r, const Theme& th, float dpi) {
    WB& w = wb();
    trackGeometry(dpi);
    ImGui::SetNextWindowPos(ImVec2((float)r.x, (float)r.y));
    ImGui::SetNextWindowSize(ImVec2((float)r.w, (float)r.h));
    ImGui::Begin("##wb", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_MenuBar);

    drawMenuBar(dpi);
    drawProjectBar(th, dpi);
    drawActionArea(th, dpi);
    drawSummary(th, dpi);

    UiState st = uiState();
    if (w.compact) {
        // 只有失败时才自动长高把错误露出来；成功 / 空闲收回原高度
        bool wantGrow = (st == StFail);
        if (wantGrow != w.grown) {
            w.grown = wantGrow;
            w.fitFrames = 3;
        }
        if (w.grown) drawErrorPeek(th);
        // 窗口高度贴着内容走：画完最后一段，光标停在哪儿就该多高
        fitCompactHeight(dpi, ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y);
    } else {
        drawLogPane(th, dpi);
    }

    // 对话框：菜单里只置旗标，菜单收起来之后才 OpenPopup（不然 popup 开在菜单的 ID 栈里）
    if (w.openNewProject) {
        ImGui::OpenPopup("新建工程");
        w.openNewProject = false;
    }
    if (w.openExportApp) {
        ImGui::OpenPopup("导出应用程序");
        w.openExportApp = false;
    }
    if (w.openExportProject) {
        ImGui::OpenPopup("导出工程");
        w.openExportProject = false;
    }
    if (w.openRunArgs) {
        ImGui::OpenPopup("运行参数");
        w.openRunArgs = false;
    }
    if (w.openAbout) {
        ImGui::OpenPopup("关于 Easel");
        w.openAbout = false;
    }
    drawNewProjectPopup(dpi);
    drawExportAppPopup(dpi);
    drawExportProjectPopup(dpi);
    drawRunArgsPopup(dpi);
    drawAboutPopup(dpi);

    ImGui::End();
}

// ------------------------------------------------------------------ 命令行
// 工作台的参数表是固定的、由我们自己维护，所以这里既能打出完整用法，也能对认不出的
// 参数直接报错退出（作品那边不行，见 src/app.cpp 构造函数里的注释）。
//
// 这两张表就是「工作台认识哪些参数」的唯一事实来源：改参数时记得同步，否则新参数会
// 被自己的未知参数检查挡掉。
// 带值的：后面紧跟一个词是它的值（--new <目录>），也可以写成 --new=<目录>
const char* const kValueFlags[] = {"new",   "name",       "example", "build",  "run",
                                   "package", "export",   "clean",   "out",    "args",
                                   "seed",    "frames",   "screenshot", "fps", "warmup",
                                   "open",    "case"};
// 不带值的开关
const char* const kBoolFlags[] = {"help", "version", "json",  "verbose", "tests", "full",
                                  "doctor", "quiet", "debug", "solve"};

bool inList(const std::string& name, const char* const* list, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (name == list[i]) return true;
    return false;
}
bool knownFlag(const std::string& name) {
    return inList(name, kValueFlags, sizeof kValueFlags / sizeof *kValueFlags) ||
           inList(name, kBoolFlags, sizeof kBoolFlags / sizeof *kBoolFlags);
}
bool takesValue(const std::string& name) {
    return inList(name, kValueFlags, sizeof kValueFlags / sizeof *kValueFlags);
}

std::string usageText(const std::string& prog) {
    std::string p = prog.empty() ? "easel" : prog;
    return "用法：" + p + " [工程目录]            打开工作台窗口（不给目录就是上次那个）\n"
           "      " + p + " <命令> [参数]         不开窗口，做完就退出\n"
           "\n"
           "命令（选一个，都不开窗口）：\n"
           "  --new <放哪的目录>      新建一个工程，配合 --name / --full / --example / --tests\n"
           "  --build <工程目录>      编译这个工程\n"
           "  --run <工程目录>        运行：编译后把作品跑起来，配合 --args \"...\" 给作品传参数\n"
           "  --package <工程目录>    导出应用程序：编一个能直接发给别人双击运行的版本\n"
           "  --export <工程目录>     导出一份能独立编译的完整源码工程，配合 --out\n"
           "  --clean <工程目录>      清理：删掉这个工程的构建产物（不动 dist/，也不动你的代码）\n"
           "  --doctor                打印环境自检（Easel 在哪、编译器、cmake、字体…）\n"
           "  -h, --help              打印这份用法\n"
           "  -V, --version           打印版本\n"
           "\n"
           "参数：\n"
           "  --name <作品名>         --new 用：作品名 = 目录名，只能用英文字母/数字/_/-\n"
           "  --full                  --new 用：建算法骨架（读数据、逐帧回放、收敛曲线），\n"
           "                          不给就是空白工程（一个文件，画个 Hello）\n"
           "  --example <示例名>      --new 用：从自带示例建（名字写错时会把可用的示例都列出来）\n"
           "  --tests                 --new 用：多带一个 tests/test_solver.cpp\n"
           "  --args \"<参数>\"         --run 用：原样传给作品的命令行参数\n"
           "  --out <目录>            --export 用：导到哪儿（默认 <工程>/dist/<名字>）\n"
           "  --json                  机器可读输出：stdout 上只有一条 JSON（配合 --build /\n"
           "                          --run / --package / --export / --clean）\n"
           "  --verbose               多打一截排障细节（PATH、候选路径、工具链探测过程）\n"
           "\n"
           "例子：\n"
           "  " + p + " --new ~/projects --name MySketch          新建一个空白工程\n"
           "  " + p + " --new ~/projects --name Ex --example sort  从 sort 示例建\n"
           "  " + p + " --build ~/projects/MySketch --json         编译，拿 JSON 结果\n"
           "  " + p + " ~/projects/MySketch                        打开工作台，选中这个工程\n"
           "\n"
           "工作台本身也是一个 Easel 程序，所以 --frames / --screenshot / --fps / --warmup /\n"
           "--seed / --quiet 这些框架参数一样认（各自的含义见作品的 app --help）。\n";
}

// 认不出的参数直接退出，不开窗口：以前任何认不出的参数都是静默地弹出工作台窗口，
// `easel --help` 敲下去只看到两行日志和一个窗口，人根本不知道自己敲错了。
// 返回 true 表示「已经处理完毕，main 应当按 *exitCode 退出」。
bool handleEarlyArgs(int argc, char** argv, int* exitCode) {
    std::string prog = argc > 0 && argv[0] ? baseName(argv[0]) : "easel";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i] ? argv[i] : "";
        if (a == "--help" || a == "-h") {
            std::fputs(usageText(prog).c_str(), stdout);
            *exitCode = 0;
            return true;
        }
        if (a == "--version" || a == "-V") {
            std::printf("%s\n", versionLine().c_str());
            *exitCode = 0;
            return true;
        }
    }
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i] ? argv[i] : "";
        if (a.rfind("--", 0) == 0) {
            std::string name = a.substr(2);
            size_t      eq = name.find('=');
            bool        inlineValue = eq != std::string::npos;
            if (inlineValue) name = name.substr(0, eq);
            if (knownFlag(name)) {
                // 带值的参数要把它后面那个词整个跳过，别再拿去做「认不认识」的判断：
                // 值本身完全可能长得像参数，`easel --run <工程> --args "--frames 30"`
                // 就是最常见的一例。这里的吞法和 cli::args().str() 取值的方式保持一致
                // （紧跟的下一个词就是值，不管它长什么样），免得两边对同一条命令行的
                // 理解不一样。
                if (takesValue(name) && !inlineValue && i + 1 < argc && argv[i + 1]) ++i;
                continue;
            }
        } else if (a.size() > 1 && a[0] == '-' && !std::isdigit((unsigned char)a[1])) {
            // 单横杠的短参数：只有 -h / -V 两个，上面已经处理掉了
        } else {
            continue;   // 位置参数（工程目录）或者带值参数的值
        }
        std::fprintf(stderr, "不认识的参数：%s\n\n", a.c_str());
        std::fputs(usageText(prog).c_str(), stderr);
        *exitCode = 2;
        return true;
    }
    return false;
}

// --example <名字>：示例名不存在时，列出真正可用的那几个。
// 以前这里不判断，直接把 <easelDir>/examples/<名字> 拼出来交给 createProject()，
// 于是报错变成「示例目录里没有 main.cpp：/私有路径/.../Resources/easel/examples/nosuchthing」
// —— 怪错了对象（不是缺 main.cpp，是压根没这个示例）、泄漏了用户不该看见的程序内部
// 路径、而且不告诉人有哪些可选。
bool checkExampleName(const std::string& want, const std::vector<std::string>& have,
                      std::string* err) {
    for (const std::string& e : have)
        if (e == want) return true;
    if (want.empty()) {
        *err = "--example 后面要跟一个示例名。";
    } else {
        *err = "没有名叫 " + want + " 的示例。";
    }
    if (have.empty()) {
        *err += "这份 Easel 里一个示例都没找到（装的可能是不带 examples/ 的精简包）。"
                "不加 --example 就是空白工程，加 --full 是算法骨架。";
    } else {
        *err += "可以用的示例有：";
        for (size_t i = 0; i < have.size(); ++i) *err += (i ? "、" : "") + have[i];
        *err += "。比如 --example " + have[0] +
                "。不加 --example 就是空白工程，加 --full 是算法骨架。";
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    // --help / --version / 认不出的参数：在构造 App 之前处理掉。构造 App 会打开日志钩子、
    // 打一行随机种子，run() 还会真的开窗口 —— `easel --help` 不该看到这些，更不该看到
    // 一个窗口。这里返回 true 就直接按它给的退出码走。
    {
        int code = 0;
        if (handleEarlyArgs(argc, argv, &code)) return code;
    }

    // --json 模式要求 stdout 上只有那一条 JSON——但 App 的构造函数会顺手调
    // cli::parse()，它打的「本次随机种子」那行 EASEL_LOG 默认也是走 stdout。
    // 这里先手动扫一眼 argv（比 App 构造还早），--json 在就提前开 quiet，
    // 把这类 Info 级别的杂音摁下去（Warn/Error 走 stderr，不受影响）。
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--json" || a.rfind("--json=", 0) == 0) {
            easel::quiet(true);
            break;
        }
    }
    App app(argc, argv);
    // 默认 960x600（docs/ui-design.md 4.1）。上次的尺寸在 loadConfig() 之后再覆盖进去——
    // 那时候才知道存了什么。
    app.title("Easel").size(M::kWinW, M::kWinH).theme(Theme::Forest()).editorEnabled(false)
       .debugConsoleEnabled(false)    // 工具类程序不需要调试台
       .idleThrottle(true)            // 界面静止的时候（没人点、也没有子进程在编译/跑）降到 10 帧省电
       .busyWhen([]{ return wb().proc.running(); });   // 编译/运行中也保持满帧
    // 底部状态栏关掉：它的「状态灯 + 状态文字」和摘要行是同一件事的两份事实来源，
    // 缩放倍数对工具类程序也没意义。状态只由摘要行说（docs/ui-design.md 4.1）。
    app.statusBar(false);
    internal::setVerbose(cli::args().has("verbose"));   // --verbose；GUI 的「详细输出」也改它

    WB& w = wb();
    // easelDir 解析：复用 --doctor 横幅那条路（editorPaths()，resolvePaths() 已经把
    // macOS .app bundle 的 .../Contents/Resources/easel 当第一候选探测过了），别在这
    // 另起一套。之前这里只认「源码构建」（EASEL_SOURCE_DIR）和「Windows 绿色工具箱」
    // 两种布局，装进 .app 里跑起来两个都不命中，w.easelDir 就一直是空——「新建工程」
    // 报「找不到工程模板」就是这个（joinPath("", "template") 退化成裸的 "template"）。
    // 装在 .app 里的程序理应优先用自己 Resources 里那份，EASEL_SOURCE_DIR 和工具箱
    // 布局垫底；每个候选仍然要「目录里确实有 template」才收，不然一个不带模板的目录
    // 会被当真。
    auto hasTemplate = [](const std::string& dir) {
        return !dir.empty() && existsU8(joinPath(dir, "template"));
    };
    if (hasTemplate(editorPaths().easelDir)) w.easelDir = editorPaths().easelDir;
#if defined(EASEL_SOURCE_DIR)
    if (w.easelDir.empty() && hasTemplate(EASEL_SOURCE_DIR)) w.easelDir = EASEL_SOURCE_DIR;
#endif
    if (w.easelDir.empty()) {
        // 工具箱布局：<工具箱>/easel/
        const Toolchain& tc = toolchain();
        if (!tc.kit.empty() && hasTemplate(joinPath(tc.kit, "easel")))
            w.easelDir = joinPath(tc.kit, "easel");
    }
    loadConfig();
    // 上次的窗口尺寸。位置要等窗口真的起来之后再看「现在还看得见吗」（onStart 里）。
    // 尺寸的上限不在这儿管——src/backend.cpp 的 computeWindowGeometry() 会把它等比夹进
    // 显示器可用区域，这里再夹一遍就是第二份实现（docs/ui-design.md 4.5）。
    app.size(w.compact ? M::kCompactW : w.fullW, w.compact ? M::kMinCompactH : w.fullH);

    // 新建工程对话框的「骨架」下拉里，示例那几项：扫 <easelDir>/examples 下每个带 main.cpp 的子目录
    if (!w.easelDir.empty()) {
        std::string exDir = joinPath(w.easelDir, "examples");
        if (isDirU8(exDir))
            for (const DirEntry& e : listDirU8(exDir))
                if (e.isDir && existsU8(joinPath(joinPath(exDir, e.name), "main.cpp")))
                    w.exampleNames.push_back(e.name);
        std::sort(w.exampleNames.begin(), w.exampleNames.end());
    }

    // workbench --new <放哪的目录> --name <作品名> [--full | --example <名字>]：
    // 不开窗口，建完就走（CI 和老师批量建工程用）
    if (cli::args().has("new")) {
        NewProjectOptions o;
        o.easelDir = w.easelDir;
        o.parentDir = absPath(cli::args().str("new"));
        o.name = cli::args().str("name", "MySketch");
        o.withTests = cli::args().has("tests");
        o.fullSkeleton = cli::args().has("full");
        if (cli::args().has("example")) {
            // 先验示例名存不存在（w.exampleNames 就是上面扫 <easelDir>/examples 扫出来的、
            // 带 main.cpp 的那些子目录），再去拼路径。见 checkExampleName() 的注释。
            std::string want = cli::args().str("example"), err;
            if (!checkExampleName(want, w.exampleNames, &err)) {
                std::printf("建不出来：%s\n", err.c_str());
                return 1;
            }
            o.exampleDir = joinPath(joinPath(w.easelDir, "examples"), want);
        }
        NewProjectReport rep = createProject(o);
        std::printf("%s\n", rep.ok ? ("建好了：" + rep.dir + "（" + std::to_string(rep.files) +
                                       " 个文件）").c_str()
                                    : ("建不出来：" + rep.error).c_str());
        return rep.ok ? 0 : 1;
    }

    // 无头命令行：--build / --run / --package / --export（都在 App 构造之后、
    // app.run() 之前处理，处理完直接 return，不开窗口）。见上面 headless:: 里的注释。
    bool jsonOut = cli::args().has("json");
    bool headlessCmd = cli::args().has("build") || cli::args().has("run") ||
                       cli::args().has("package") || cli::args().has("export") ||
                       cli::args().has("clean");
    if (headlessCmd) {
        // App 的构造函数已经无条件装好了日志钩子（installHooks(false)，给调试台用），
        // 装上之后 EASEL_LOG 一律经 hookLog -> writeThrough 直通终端，不再看 quiet()。
        // 这几个无头命令没有调试台可存，也不该被这条直通路径绕过 --json 的“只输出一条
        // JSON”约定（比如 exportProject() 结尾那条 EASEL_LOG），所以先把钩子摘掉，
        // 之后的 EASEL_LOG 就会退回 core.h 里那条认 quiet() 的老路。只在真的要走无头
        // 命令时才摘，不影响 GUI 路径（App::run() 稍后会自己重新装上 installHooks(true)）。
        internal::removeHooks();
    }
    if (cli::args().has("build")) return headless::cmdBuild(cli::args().str("build"), jsonOut);
    if (cli::args().has("run")) return headless::cmdRun(cli::args().str("run"), jsonOut);
    if (cli::args().has("package")) return headless::cmdPackage(cli::args().str("package"), jsonOut);
    if (cli::args().has("export")) return headless::cmdExport(cli::args().str("export"), jsonOut);
    if (cli::args().has("clean")) return headless::cmdClean(cli::args().str("clean"), jsonOut);

    if (!cli::args().at(0).empty()) useProject(absPath(cli::args().at(0)));

    // 工作台一启动就打启动横幅，在输出区最上面——查问题时第一眼就看得到用的是哪个
    // Easel、哪份预编译、编译器/cmake/ninja 在哪儿（D-新，第 1 部分）。
    // 「详细输出」：命令行 --verbose 优先，其次用上次记下来的（loadConfig 读的那一项）
    if (cli::args().has("verbose")) w.verboseUI = true;
    internal::setVerbose(w.verboseUI);
    beginLog();
    w.status = w.projectDir.empty() ? "先新建一个工程，或者打开一个已有的" : "工程：" + baseName(w.projectDir);

    // 文档 / 排障用的启动钩子（不是给学生的功能，所以不占一个命令行参数）：
    //   EASEL_WB_ARGS=…        启动时把运行参数填进去（等价于「设置 → 运行参数…」里手填）
    //   EASEL_WB_AUTORUN=…     启动后自动点一次某个菜单项 / 按钮：
    //                          run（运行）、clean（清理）、doctor（环境自检）、new（新建工程…）、
    //                          exportapp（导出应用程序…）、exportproject（导出工程…）、
    //                          runargs（运行参数…）、about（关于 Easel）
    // 截图和冒烟测试靠它把界面开到某个状态，不用人动手，也不往界面里塞假数据。
    if (const char* a = std::getenv("EASEL_WB_ARGS")) std::snprintf(w.runArgs, sizeof w.runArgs, "%s", a);
    std::string autorun;
    if (const char* act = std::getenv("EASEL_WB_AUTORUN")) autorun = act;

    app.onStart([&app, autorun] {
        float dpi = (float)app.dpiScale();
        // 窗口起来了才能问 glfw：上次的位置在现在这套显示器上还看得见吗（看不见就不还原）
        restoreWindowPos();
        applyAlwaysOnTop();
        applySizeLimits(dpi);
        if (wb().compact) wb().fitFrames = 3;   // 紧凑模式：第一帧之后把高度贴合内容
        WB& w2 = wb();
        if (autorun == "run") {
            runOrRerun();
        } else if (autorun == "clean") {
            doClean();
        } else if (autorun == "doctor") {
            runDoctor(dpi);
        } else if (autorun == "new") {
            if (!w2.newParent[0])
                std::snprintf(w2.newParent, sizeof w2.newParent, "%s", defaultProjectsDir().c_str());
            w2.openNewProject = true;
        } else if (autorun == "exportapp") {
            w2.openExportApp = true;
        } else if (autorun == "exportproject") {
            std::snprintf(w2.exportOutDir, sizeof w2.exportOutDir, "%s",
                          joinPath(joinPath(w2.projectDir, "dist"), baseName(w2.projectDir)).c_str());
            w2.openExportProject = true;
        } else if (autorun == "runargs") {
            w2.openRunArgs = true;
        } else if (autorun == "about") {
            w2.openAbout = true;
        }
    });
    app.onFrame([](double) { pump(); });
    app.onWindow([&app](const Rect& r) { draw(r, app.theme(), (float)app.dpiScale()); });
    app.onKey([](Key key) {
        // F5 永远是「让我看见我的作品」：作品正在跑就是重跑（docs/ui-design.md 2.2）
        if (key == Key::F5) runOrRerun();
    });

    int rc = app.run();
    saveConfig();   // 窗口几何每帧只记在内存里（trackGeometry），退出时才写一次盘
    return rc;
}
