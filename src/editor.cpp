// Easel — editor.cpp  左边那一栏：改 solver.cpp，按 F5 编译并运行（D-28）
//
// 它不是 IDE，故意不是：没有断点、没有补全、没有文件树、只开一个文件。
// 它只做三件事 ——
//   1. 语法高亮地改 src/solver.cpp（学生唯一写算法的文件，D-23）；
//   2. F5 = 保存 + g++ 编命令行版 solver + 跑起来，输出落在下面那半屏；
//   3. 报错行号可点，直接跳到出错那一行，旁边留个红标记。
// 编的是命令行的 solver，不是 App 自己（D-27 第 3 条：不碰热重载、不碰自我覆盖）。
#include "internal.h"

#include <TextEditor.h>

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace easel {
namespace internal {

namespace {

enum Kind { KindPlain = 0, KindError = 1, KindWarn = 2, KindSys = 3 };
enum Stage { StageIdle = 0, StageCompile = 1, StageRun = 2 };

struct OutLine {
    std::string text;
    int         kind = KindPlain;
    std::string file;          // 解析出来的位置，空 = 这行不能点
    int         line = 0, col = 0;
};

struct Ed {
    TextEditor* te = nullptr;
    EditorPaths paths;
    std::vector<std::string> pathDiag;   // resolvePaths() 试过的候选，verbose 时打出来
    bool        pathsResolved = false;
    bool        loaded = false;
    std::string loadError;

    size_t    savedUndoIndex = 0;
    long long diskMtime = 0, diskSize = 0;
    bool      externalChange = false;
    double    lastStampCheck = 0;

    Proc                 proc;
    int                  stage = StageIdle;
    std::string          pending;      // 还没换行的半行
    std::vector<OutLine> out;
    size_t               outLimit = 4000;
    bool                 outFollow = true;
    double               startedAt = 0, compileSeconds = 0;
    int                  errors = 0, warnings = 0;
    std::string          runExe;
    char                 runArgs[256] = {0};

    bool  markersDirty = false;
    float outH = 200.f;
    float fontScale = 1.f;
    bool  themeApplied = false;
    bool  wantExport = false;      // 下一帧真的去导出（这一帧先把「导出中…」画出来）
    ExportOptions exportOpt;
    char  exportName[128] = {0};
    char  exportDir[512] = {0};
    bool  exportWithEasel = true, exportVerify = true;
    bool  exportRunning = false;
};

Ed& ed() {
    static Ed e;
    return e;
}

std::string exeSuffix() {
#if defined(_WIN32)
    return ".exe";
#else
    return "";
#endif
}

// ------------------------------------------------------------------ 路径
// 编译期常量指出工程在哪（CMake 传的）；exe 被搬到别的机器上时退回当前目录。
std::string firstExisting(const std::vector<std::string>& cands) {
    for (const std::string& c : cands)
        if (!c.empty() && existsU8(c)) return c;
    return {};
}

}  // namespace

// looksLikeProject 要给 tests/test_editor.cpp 直接调，不能待在匿名命名空间里
// （声明在 internal.h）。
//
// 光查 src/app.cpp 存在不够严——Easel 仓库自己的 src/ 底下也有一个 app.cpp（库内部
// 实现 App 类的那个，凑巧同名），会把仓库自己误判成工程目录。以前用「app.cpp 和
// solver.cpp 都要在」来排除，但极简工程（template-hello，一个文件，没有
// solver.cpp）也会被这条一起挡在外面，误判成「不是工程」。
// 换一个只有 Easel 仓库自己才有、学生工程不可能有的反向特征：整棵
// include/easel/ 源码树（比如 include/easel/easel.h）。学生工程无论是导出包还是
// 开发中的 .easel/ 折叠布局，都只有摊平的单头库或散装的几个头文件，不会带上完整
// 的 include/easel/ 目录。
bool looksLikeProject(const std::string& dir) {
    if (dir.empty()) return false;
    if (!existsU8(joinPath(dir, "src/app.cpp"))) return false;
    return !existsU8(joinPath(dir, "include/easel/easel.h"));
}

namespace {

void resolvePaths(Ed& e) {
    if (e.pathsResolved) return;
    e.pathsResolved = true;

#if defined(EASEL_PROJECT_DIR)
    std::string compiledProject = EASEL_PROJECT_DIR;
#else
    std::string compiledProject;
#endif
#if defined(EASEL_SOURCE_DIR)
    std::string compiledEasel = EASEL_SOURCE_DIR;
#else
    std::string compiledEasel;
#endif

    // 用预编译的 Easel（<工具箱>/easel/prebuilt，D-26）时，libeasel.a 是在**打包那台
    // 机器**上编的，上面两个编译期常量指的都是打包机的路径 —— 在学生机上要么不存在，
    // 要么（最坏情况：就在打包那台机器上做实验，比如这次 macOS 发布包就撞上了）指到
    // 打包那个人电脑上一个不相关的工程/仓库去。所以这里的顺序是「运行期看得见的东西
    // 优先，编译期常量垫底」：
    //   工程目录 ← ① 当前目录。工作台（D-29）起作品子进程时把工作目录设成工程根，
    //                 学生双击 exe 也一样，所以这条在预编译路径下就是正解；
    //              ② 编译期常量（源码构建 / add_subdirectory 走的路，一直是对的）；
    //              ③ exe 的上一级（build/bin/app → build/，少见但留着）；
    //              ④ 全没匹配上：留空，别瞎猜——见下面 e.paths.projectDir 那段注释。
    //   Easel 源码 ← 运行期从 exe 往上翻找 easel/ 目录（见下面那段），翻不到才退到
    //                编译期常量。
    // 「导出源码」（--export）和 F9 编辑栏认的就是这两个值。
    // 光看候选目录底下有没有 src/ 不够严——比如从 easel 仓库根目录起进程时，仓库自己
    // 的 src/ 也存在，会把 projectDir 错判成 easel 本身。所以每个候选都要过
    // looksLikeProject()：目录底下得有 src/app.cpp，而且不能是 easel 仓库自己
    // （靠有没有 include/easel/easel.h 分辨）。顺序不变，仍是 cwd 优先。
    std::string cwd = fs::cwd();
    e.paths.projectDir.clear();
    e.pathDiag.push_back("---- projectDir 候选（顺序：cwd -> 编译期常量 -> exe 上一级）----");
    for (const std::string& cand : {cwd, compiledProject, fs::dirOf(exeDir())}) {
        if (cand.empty()) {
            e.pathDiag.push_back("(空，跳过)");
            continue;
        }
        bool ok = looksLikeProject(cand);
        e.pathDiag.push_back(cand + " -> " +
                             (ok ? "像工程（有 src/app.cpp，且不是 easel 仓库自己），采用"
                                 : "不像工程（缺 src/app.cpp，或者是 easel 仓库自己）"));
        if (ok && e.paths.projectDir.empty()) e.paths.projectDir = cand;
    }
    if (e.paths.projectDir.empty()) e.pathDiag.push_back("都不像，projectDir 留空（不瞎猜）");
    // 全没匹配上就是真没打开工程（比如刚解压发布包、双击 Easel.app 看看而已），留空，
    // 别退回 cwd —— 之前这里退回过 cwd，但 GLFW 的 Cocoa 后端默认会在起窗口时把进程
    // cwd chdir 到 <Easel.app>/Contents/Resources/（vendor/glfw/src/cocoa_init.m 的
    // changeToResourcesDirectory()，GLFW_COCOA_CHDIR_RESOURCES 默认开），所以 mac
    // 发布包上这个「退回 cwd」的兜底实际退回的是 Resources/，doctor 里就会看到一个
    // 一本正经但完全错误的工程目录。留空之后 doctor() / app.cpp 里判断
    // ep.projectDir.empty() 打印「(没打开工程)」，比给一个错的路径诚实。
    // 注：--export 是在 backend::init()（会走到 glfwInit）**之前**处理的（见
    // App::run()），所以「作品自己导出自己」时 cwd 还是调用者原本的 cwd，没被
    // GLFW chdir 污染，这条路径不受影响，下面的候选顺序也没变。

    // Easel 源码：「就近优先」——运行时看得见的东西打败编译期常量。
    //   ① macOS .app bundle 布局（仅 __APPLE__ 下编译）：exeDir() 是
    //      .../Contents/MacOS，源码树收在 .../Contents/Resources/easel/ 里（A 节，
    //      macOS 发布包不再是 <包根>/Easel.app 和 <包根>/easel/ 并排——包根摆一个
    //      60MB 的 easel/ 目录不像个正常 Mac 应用，现在整棵源码树 + prebuilt/ 都
    //      收进 app 内部）。这条必须排在下一条前面：下一条会往上翻好几层，bundle
    //      场景下翻上去就是 /Applications，运气不好会撞见别的同名目录；
    //   ② 从 exe 往上翻几层找 easel/ 源码树（跟 toolchain.cpp 找绿色工具箱是同一套
    //      办法：工具箱布局是 <工具箱>/easel/ 和 w64devkit/ cmake/ ninja/ 并排，
    //      源码构建 / 非 bundle 场景走这条，几层就到）；
    //   ③ toolchain().kit（findCxx() 顺路翻出来的工具箱根目录，省一遍遍历；Mac 包没
    //      有 w64devkit/，kit 会是空，下面判空跳过就好，不算错误）；
    //   ④ 工程目录旁边（开发时常见摆法：workspace/easel + workspace/myproject，源码
    //      构建、非顶层 add_subdirectory 走这条）；
    //   ⑤ 编译期常量 EASEL_SOURCE_DIR 垫底——它只在「源码构建 easel 本身」这条路上
    //      才指向正确的地方；预编译包发布出去之后，这个编译期常量在别人机器上要么
    //      不存在，要么指向打包那台机器上的路径（这次事故就是指到了打包机的仓库）。
    // 每个候选都要经过同一套「真源码树」判定：<候选>/CMakeLists.txt 和
    // <候选>/include/easel/easel.h 都要在——光有个同名目录不算，「导出源码」要从
    // 这儿拷 CMakeLists / include / src / vendor 的许可证。
    auto looksLikeEaselTree = [](const std::string& cand) {
        return existsU8(joinPath(cand, "CMakeLists.txt")) &&
               existsU8(joinPath(cand, "include/easel/easel.h"));
    };
    e.paths.easelDir.clear();
    {
        std::vector<std::string> dirs;
#if defined(__APPLE__)
        // bundle 布局排第一（见上面①）：exeDir() = .../Contents/MacOS，源码树是
        // .../Contents/Resources/easel，常数层级，不用翻找。
        dirs.push_back(joinPath(fs::dirOf(exeDir()), "Resources/easel"));
#endif
        std::string dir = exeDir();
        for (int up = 0; up < 6 && !dir.empty(); ++up) {
            dirs.push_back(joinPath(dir, "easel"));
            std::string parent = fs::dirOf(dir);
            if (parent == dir) break;
            dir = parent;
        }
        if (!toolchain().kit.empty()) dirs.push_back(joinPath(toolchain().kit, "easel"));
        if (!e.paths.projectDir.empty()) {
            dirs.push_back(joinPath(e.paths.projectDir, "easel"));
            dirs.push_back(joinPath(fs::dirOf(e.paths.projectDir), "easel"));
        }
        dirs.push_back(compiledEasel);
        e.pathDiag.push_back("---- easelDir 候选（就近优先，编译期常量垫底）----");
        for (const std::string& cand : dirs) {
            if (cand.empty()) continue;
            bool ok = looksLikeEaselTree(cand);
            e.pathDiag.push_back(cand + " -> " +
                                 (ok ? "是真源码树（有 CMakeLists.txt 和 include/easel/easel.h），采用"
                                     : "不是（缺 CMakeLists.txt 或 include/easel/easel.h）"));
            if (ok && e.paths.easelDir.empty()) e.paths.easelDir = cand;
        }
        if (e.paths.easelDir.empty()) e.pathDiag.push_back("都不是，easelDir 留空");
    }

    if (e.paths.file.empty()) {
        e.paths.file = firstExisting({joinPath(e.paths.projectDir, "src/solver.cpp"),
                                      joinPath(e.paths.projectDir, "solver.cpp")});
    }
    e.paths.ok = !e.paths.file.empty() && existsU8(e.paths.file);
}

// ------------------------------------------------------------------ 输出
void addOut(Ed& e, const std::string& text, int kind, const std::string& file = {}, int line = 0,
            int col = 0) {
    OutLine o;
    o.text = text;
    o.kind = kind;
    o.file = file;
    o.line = line;
    o.col = col;
    e.out.push_back(std::move(o));
    if (e.out.size() > e.outLimit) e.out.erase(e.out.begin(), e.out.begin() + 200);
    e.outFollow = true;
}

void addSys(Ed& e, const std::string& text) { addOut(e, text, KindSys); }

}  // namespace

// ------------------------------------------------------------------ 诊断解析
// 认得出这几种：
//   src/solver.cpp:12:5: error: ...        （gcc / clang，英文）
//   src\solver.cpp:12:5: 错误：...          （中文 locale 的 gcc）
//   C:/x/src/solver.cpp:12: warning: ...
// 认不出来就当普通输出，不做花活 —— 一个正则的钱办一件事（D-27 第 4 条）。
bool parseDiagnostic(const std::string& s, std::string* file, int* line, int* col, int* kind) {
    struct KW {
        const char* text;
        int         kind;
    };
    static const KW kws[] = {{" error:", KindError},   {" 错误：", KindError},
                             {" fatal error:", KindError}, {" warning:", KindWarn},
                             {" 警告：", KindWarn},     {" note:", KindPlain},
                             {" 附注：", KindPlain}};
    size_t best = std::string::npos;
    int    bestKind = KindPlain;
    for (const KW& k : kws) {
        size_t p = s.find(k.text);
        if (p != std::string::npos && (best == std::string::npos || p < best)) {
            best = p;
            bestKind = k.kind;
        }
    }
    if (best == std::string::npos) return false;

    std::string loc = s.substr(0, best);
    while (!loc.empty() && (loc.back() == ':' || loc.back() == ' ')) loc.pop_back();
    if (loc.empty()) return false;

    // 从右边啃数字：":行:列" 或 ":行"
    int  nums[2] = {0, 0};
    int  got = 0;
    while (got < 2) {
        size_t p = loc.find_last_of(':');
        if (p == std::string::npos || p + 1 >= loc.size()) break;
        std::string tail = loc.substr(p + 1);
        bool        digits = !tail.empty();
        for (char c : tail)
            if (!std::isdigit((unsigned char)c)) digits = false;
        if (!digits) break;
        nums[got++] = std::atoi(tail.c_str());
        loc.resize(p);
    }
    if (got == 0 || loc.empty()) return false;
    // Windows 的盘符 "C" 不是文件名
    if (loc.size() == 1) return false;

    if (file) *file = loc;
    if (line) *line = got == 2 ? nums[1] : nums[0];
    if (col) *col = got == 2 ? nums[0] : 0;
    if (kind) *kind = bestKind;
    return true;
}

namespace {

void feedOutput(Ed& e, const std::string& chunk) {
    e.pending += chunk;
    size_t start = 0;
    for (;;) {
        size_t nl = e.pending.find('\n', start);
        if (nl == std::string::npos) break;
        std::string line = e.pending.substr(start, nl - start);
        start = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string f;
        int         ln = 0, cl = 0, kind = KindPlain;
        if (parseDiagnostic(line, &f, &ln, &cl, &kind)) {
            if (kind == KindError) ++e.errors;
            if (kind == KindWarn) ++e.warnings;
            addOut(e, line, kind, f, ln, cl);
        } else {
            addOut(e, line, KindPlain);
        }
    }
    e.pending.erase(0, start);
}

// ------------------------------------------------------------------ 读写文件
bool loadFile(Ed& e) {
    std::string text;
    if (!e.paths.ok || !readTextU8(e.paths.file, &text)) {
        e.loadError = "打不开 " + e.paths.file;
        return false;
    }
    // 去掉 UTF-8 BOM：小熊猫和记事本会加，ImGui 会把它画成一个怪符号
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        text.erase(0, 3);
    e.te->SetText(text);
    e.savedUndoIndex = e.te->GetUndoIndex();
    fileStamp(e.paths.file, &e.diskMtime, &e.diskSize);
    e.externalChange = false;
    e.loaded = true;
    e.loadError.clear();
    return true;
}

bool dirty(Ed& e) { return e.te && e.loaded && e.te->GetUndoIndex() != e.savedUndoIndex; }

bool saveFile(Ed& e, std::string* err) {
    if (!e.loaded) return false;
    std::string text = e.te->GetText();
    if (!writeTextU8(e.paths.file, text)) {
        if (err) *err = "写不进去：" + e.paths.file + "（文件是不是只读，或者被别的程序占着？）";
        return false;
    }
    e.savedUndoIndex = e.te->GetUndoIndex();
    fileStamp(e.paths.file, &e.diskMtime, &e.diskSize);
    e.externalChange = false;
    return true;
}

// 别人在外面改了同一个文件（小熊猫、VS Code 都还在用，D-27 第 5 条）——
// 绝不静默覆盖，也绝不静默丢掉学生刚打的字，只把选择权交出去。
void checkExternal(Ed& e) {
    if (!e.loaded || e.externalChange) return;
    double now = ImGui::GetTime();
    if (now - e.lastStampCheck < 1.0) return;
    e.lastStampCheck = now;
    long long m = 0, sz = 0;
    if (!fileStamp(e.paths.file, &m, &sz)) return;
    if (m != e.diskMtime || sz != e.diskSize) e.externalChange = true;
}

// ------------------------------------------------------------------ 跳到某一行
void jumpTo(Ed& e, const OutLine& o) {
    if (!e.loaded || o.line <= 0) return;
    // 只认当前打开的这个文件（我们只开一个 buffer）
    std::string a = baseName(o.file), b = baseName(e.paths.file);
    if (a != b) {
        if (App::instance()) App::instance()->toast("这一行在 " + a + " 里，编辑栏只开着 " + b);
        return;
    }
    size_t line = (size_t)(o.line - 1);
    e.te->SetCursor(TextEditor::DocPos(line, o.col > 0 ? (size_t)(o.col - 1) : 0));
    e.te->ScrollToLine(line, TextEditor::Scroll::alignMiddle);
    e.te->SetFocus();
}

void refreshMarkers(Ed& e, const Theme& th) {
    if (!e.loaded) return;
    e.te->ClearMarkers();
    std::string me = baseName(e.paths.file);
    for (const OutLine& o : e.out) {
        if (o.line <= 0 || (o.kind != KindError && o.kind != KindWarn)) continue;
        if (baseName(o.file) != me) continue;
        Color c = o.kind == KindError ? th.bad : th.warn;
        ImU32 u = col(Color(c.r, c.g, c.b, 0.30f));
        e.te->AddMarker((size_t)(o.line - 1), col(c), u, o.text, o.text);
    }
}

// ------------------------------------------------------------------ 编译 / 运行
void startRun(Ed& e) {
    std::vector<std::string> argv{e.runExe};
    std::string              a = e.runArgs;
    std::string              cur;
    for (size_t i = 0; i <= a.size(); ++i) {   // 参数按空格切，够用了
        if (i == a.size() || a[i] == ' ') {
            if (!cur.empty()) argv.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(a[i]);
        }
    }
    std::string err;
    if (!e.proc.start(argv, e.paths.projectDir, &err)) {
        addOut(e, err, KindError);
        e.stage = StageIdle;
        return;
    }
    e.stage = StageRun;
    e.startedAt = ImGui::GetTime();
    addSys(e, "▶ " + e.proc.cmdline());
}

}  // namespace

// ------------------------------------------------------------------ 对外
const EditorPaths& editorPaths() {
    Ed& e = ed();
    resolvePaths(e);
    return e.paths;
}

const std::vector<std::string>& editorPathsDiag() {
    Ed& e = ed();
    resolvePaths(e);
    return e.pathDiag;
}

void setEditorFile(const std::string& path) {
    Ed& e = ed();
    resolvePaths(e);
    if (path.empty()) return;
    e.paths.file = absPath(path);
    e.paths.ok = existsU8(e.paths.file);
    e.loaded = false;
}

bool editorBusy() { return ed().proc.running(); }

void editorRun() {
    Ed& e = ed();
    resolvePaths(e);
    if (e.proc.running()) {
        addSys(e, "上一次还在跑。先按「停止」。");
        return;
    }
    if (!e.loaded) {
        addSys(e, "还没打开文件：" + (e.paths.file.empty() ? std::string("找不到 src/solver.cpp")
                                                           : e.paths.file));
        return;
    }
    std::string err;
    if (dirty(e) && !saveFile(e, &err)) {
        addOut(e, err, KindError);
        return;
    }

    const Toolchain& tc = toolchain();
    if (tc.cxx.empty()) {
        addOut(e, tc.note, KindError);
        return;
    }

    std::string outDir = joinPath(joinPath(e.paths.projectDir, "build"), "run");
    if (!makeDirsU8(outDir)) {
        addOut(e, "建不出目录：" + outDir, KindError);
        return;
    }
    e.runExe = joinPath(outDir, "solver" + exeSuffix());

    e.errors = e.warnings = 0;
    e.out.clear();
    e.pending.clear();
    if (e.te) e.te->ClearMarkers();

    std::vector<std::string> argv{tc.cxx,
                                  "-std=c++17",
                                  "-DEASEL_STANDALONE",
                                  "-g",
                                  "-O1",
                                  "-fdiagnostics-color=never",
                                  "-I" + joinPath(e.paths.projectDir, "src"),
                                  e.paths.file,
                                  "-o",
                                  e.runExe};
    addSys(e, "$ " + Proc::describe(argv));
    if (!e.proc.start(argv, e.paths.projectDir, &err)) {
        addOut(e, err, KindError);
        return;
    }
    e.stage = StageCompile;
    e.startedAt = ImGui::GetTime();
}

void editorPump() {
    Ed& e = ed();

    // 上一帧点了「导出」：那一帧已经把「正在导出…」画出去了，这一帧真干活
    if (e.wantExport) {
        e.wantExport = false;
        ExportReport r = exportProject(e.exportOpt);
        addSys(e, r.ok ? "导出完成：" + r.outDir : "导出失败：" + r.error);
        for (const std::string& l : r.checks) addOut(e, l, l.find("[×]") == 0 ? KindError
                                                          : l.find("[!]") == 0 ? KindWarn : KindPlain);
        e.exportRunning = false;
        if (App::instance()) App::instance()->toast(r.ok ? "导出好了，看下面的自检" : "导出失败");
    }

    if (e.stage == StageIdle && !e.proc.running()) return;

    std::string chunk;
    bool        alive = e.proc.pump(&chunk);
    if (!chunk.empty()) feedOutput(e, chunk);
    if (alive) return;

    if (!e.pending.empty()) {
        feedOutput(e, "\n");
        e.pending.clear();
    }
    int    code = e.proc.exitCode();
    double secs = ImGui::GetTime() - e.startedAt;

    if (e.stage == StageCompile) {
        e.compileSeconds = secs;
        if (code == 0) {
            char buf[128];
            std::snprintf(buf, sizeof buf, "编译成功（%.1f 秒%s），开始运行", secs,
                          e.warnings ? ("，" + std::to_string(e.warnings) + " 个警告").c_str() : "");
            addSys(e, buf);
            startRun(e);
        } else {
            char buf[160];
            std::snprintf(buf, sizeof buf, "编译失败：%d 个错误、%d 个警告（%.1f 秒）。点上面红色那行跳过去。",
                          e.errors, e.warnings, secs);
            addOut(e, buf, KindError);
            e.stage = StageIdle;
        }
        e.markersDirty = true;
    } else if (e.stage == StageRun) {
        char buf[200];
        if (code == 0) {
            std::snprintf(buf, sizeof buf, "运行结束，正常退出（%.1f 秒）", secs);
            addSys(e, buf);
        } else if (code == -2) {
            addSys(e, "已停止");
        } else if (code >= 128) {
            std::snprintf(buf, sizeof buf,
                          "程序被信号 %d 打断（%.1f 秒）—— 多半是越界或者空指针。"
                          "用 Debug 预设跑一次，ASan 会直接指出哪一行。",
                          code - 128, secs);
            addOut(e, buf, KindError);
        } else {
            std::snprintf(buf, sizeof buf, "运行结束，退出码 %d（%.1f 秒）", code, secs);
            addOut(e, buf, KindWarn);
        }
        e.stage = StageIdle;
    }
}

// ------------------------------------------------------------------ 界面
void drawEditor(App& app, const Rect& r, bool* open) {
    Ed&          e = ed();
    const Theme& th = app.theme();
    resolvePaths(e);

    if (!e.te) {
        e.te = new TextEditor();
        e.te->SetLanguage(TextEditor::Language::Cpp());
        e.te->SetTabSize(4);
        e.te->SetInsertSpacesOnTabs(true);
        e.te->SetShowLineNumbersEnabled(true);
        e.te->SetShowMatchingBrackets(true);
        e.te->SetShowWhitespacesEnabled(false);   // 空格点、Tab 箭头：满屏小点，关掉
        e.te->SetCompletePairedGlyphs(false);   // 学生自己打括号，别替他做主
        e.te->SetShowScrollbarMiniMapEnabled(false);
        e.te->SetFindButtonLabel("查找");
        e.te->SetFindAllButtonLabel("全部");
        e.te->SetReplaceButtonLabel("替换");
        e.te->SetReplaceAllButtonLabel("全部替换");
    }
    if (!e.themeApplied) {
        e.themeApplied = true;
        TextEditor::Palette p = th.dark ? TextEditor::GetDarkPalette() : TextEditor::GetLightPalette();
        p[(size_t)TextEditor::Color::background] = col(th.surface);
        p[(size_t)TextEditor::Color::text] = col(th.fg);
        p[(size_t)TextEditor::Color::lineNumber] = col(th.muted);
        p[(size_t)TextEditor::Color::currentLineNumber] = col(th.accent);
        e.te->SetPalette(p);
    }
    if (!e.loaded && e.paths.ok) loadFile(e);
    checkExternal(e);

    float dpi = (float)app.dpiScale();
    ImGui::SetNextWindowPos(iv(r.min()));
    ImGui::SetNextWindowSize(iv({r.w, r.h}));
    ImGui::Begin("##editor", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollbar);

    // ---- 工具条 ----
    bool busy = e.proc.running();
    ImGui::BeginDisabled(busy || !e.loaded);
    if (ImGui::Button("编译并运行  F5")) editorRun();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!busy);
    if (ImGui::Button("停止")) {
        e.proc.kill();
        addSys(e, "已停止");
        e.stage = StageIdle;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty(e));
    if (ImGui::Button("保存")) {
        std::string err;
        if (!saveFile(e, &err)) addOut(e, err, KindError);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("导出工程…")) {
        if (e.exportName[0] == 0)
            std::snprintf(e.exportName, sizeof e.exportName, "%s", baseName(e.paths.projectDir).c_str());
        ImGui::OpenPopup("导出一个能独立编译的工程");
    }
    ImGui::SameLine();
    if (ImGui::Button("关闭")) *open = false;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("F9 也能开关这一栏");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("编的是命令行版 solver.cpp（-DEASEL_STANDALONE），不是这个界面程序。\n"
                          "编译器：%s\n来源：%s",
                          toolchain().cxx.empty() ? "没找到" : toolchain().cxx.c_str(),
                          toolchain().source.empty() ? toolchain().note.c_str() : toolchain().source.c_str());
    }

    // 第二行：文件名 + 运行参数 + 字号
    ImGui::PushStyleColor(ImGuiCol_Text, iv4(th.muted));
    ImGui::TextUnformatted((baseName(e.paths.file) + (dirty(e) ? "  ●未保存" : "") +
                            (busy ? (e.stage == StageCompile ? "  编译中…" : "  运行中…") : ""))
                               .c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && !e.paths.file.empty()) ImGui::SetTooltip("%s", e.paths.file.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150 * dpi);
    ImGui::InputTextWithHint("##args", "运行参数 如 --seed 7", e.runArgs, sizeof e.runArgs);
    ImGui::SameLine();
    if (ImGui::SmallButton("A-")) e.fontScale = (float)clamp(e.fontScale - 0.1, 0.7, 2.5);
    ImGui::SameLine();
    if (ImGui::SmallButton("A+")) e.fontScale = (float)clamp(e.fontScale + 0.1, 0.7, 2.5);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("编辑栏字号（也可以在代码上 Ctrl+滚轮）");

    // ---- 外部改动 ----
    if (e.externalChange) {
        ImGui::PushStyleColor(ImGuiCol_Text, iv4(th.warn));
        ImGui::TextWrapped("这个文件在编辑栏外面被改过（小熊猫？VS Code？）");
        ImGui::PopStyleColor();
        if (ImGui::Button("读外面的")) loadFile(e);
        ImGui::SameLine();
        if (ImGui::Button("留我的")) {
            fileStamp(e.paths.file, &e.diskMtime, &e.diskSize);
            e.externalChange = false;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("（「留我的」下次保存会覆盖外面的改动）");
    }

    // ---- 导出对话框 ----
    if (ImGui::BeginPopupModal("导出一个能独立编译的工程", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "导出的是一个完整工程：算法、界面、数据、素材、Easel 源码和它的全部依赖都在里面。\n"
            "评委机器上不用装 Easel、不用联网、不用改一行 CMake，解压就能编。");
        ImGui::Spacing();
        ImGui::InputText("作品名", e.exportName, sizeof e.exportName);
        ImGui::InputTextWithHint("导出到", "留空 = 工程目录下的 dist/", e.exportDir, sizeof e.exportDir);
        ImGui::Checkbox("带上 Easel 源码与依赖（评委才编得出界面，约 60 MB）", &e.exportWithEasel);
        ImGui::Checkbox("导出后试编一次 solver.cpp，证明它真的能独立编", &e.exportVerify);
        ImGui::Spacing();
        if (ImGui::Button("开始导出", ImVec2(140 * dpi, 0))) {
            e.exportOpt = ExportOptions{};
            e.exportOpt.name = e.exportName;
            e.exportOpt.outDir = e.exportDir;
            e.exportOpt.withEasel = e.exportWithEasel;
            e.exportOpt.verify = e.exportVerify;
            e.wantExport = true;
            e.exportRunning = true;
            addSys(e, "正在导出…（几十 MB，界面会卡一下）");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(100 * dpi, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // ---- 编辑区 ----
    float outH = clamp(e.outH * dpi, 60.f * dpi, r.h * 0.7f);
    float avail = ImGui::GetContentRegionAvail().y;
    float editH = avail - outH - 10 * dpi;
    if (editH < 80 * dpi) editH = 80 * dpi;

    if (e.loaded) {
        ImGui::PushFont(codeFont(), ImGui::GetStyle().FontSizeBase * e.fontScale);
        e.te->Render("##code", ImVec2(0, editH));
        ImGui::PopFont();
        if (ImGui::IsItemHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.f)
            e.fontScale = (float)clamp(e.fontScale + ImGui::GetIO().MouseWheel * 0.1, 0.7, 2.5);
    } else {
        ImGui::BeginChild("##noc", ImVec2(0, editH));
        ImGui::TextWrapped("没打开文件。%s", e.loadError.empty()
                                                 ? "编辑栏默认开 src/solver.cpp —— 这个工程里没有它。"
                                                 : e.loadError.c_str());
        ImGui::Spacing();
        if (ImGui::Button("选一个 .cpp 打开…")) {
            std::string p = file::open("C++ 源码", "cpp,h,hpp,cc");
            if (!p.empty()) setEditorFile(p);
        }
        ImGui::EndChild();
    }

    // ---- 分隔条：拖着改输出区高度 ----
    ImGui::InvisibleButton("##outsplit", ImVec2(-1, 8 * dpi));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive()) e.outH -= ImGui::GetIO().MouseDelta.y / dpi;

    // ---- 输出区 ----
    ImGui::TextUnformatted("输出");
    ImGui::SameLine();
    if (ImGui::SmallButton("清空")) {
        e.out.clear();
        e.errors = e.warnings = 0;
        if (e.te) e.te->ClearMarkers();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("复制")) {
        std::string all;
        for (const OutLine& o : e.out) all += o.text + "\n";
        ImGui::SetClipboardText(all.c_str());
    }
    if (e.errors || e.warnings) {
        ImGui::SameLine();
        ImGui::TextColored(iv4(e.errors ? th.bad : th.warn), "%d 错误 · %d 警告", e.errors, e.warnings);
    }

    ImGui::BeginChild("##out", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushFont(monoFont(), ImGui::GetStyle().FontSizeBase * 0.92f);
    for (size_t i = 0; i < e.out.size(); ++i) {
        const OutLine& o = e.out[i];
        ImVec4         c = o.kind == KindError  ? iv4(th.bad)
                           : o.kind == KindWarn ? iv4(th.warn)
                           : o.kind == KindSys  ? iv4(th.accent)
                                                : iv4(th.fg);
        ImGui::PushStyleColor(ImGuiCol_Text, c);
        if (!o.file.empty() && o.line > 0) {
            ImGui::PushID((int)i);
            if (ImGui::Selectable(o.text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
                jumpTo(e, o);
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PopID();
        } else {
            ImGui::TextUnformatted(o.text.c_str());
        }
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();
    if (e.outFollow && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.f)
        ImGui::SetScrollHereY(1.f);
    ImGui::EndChild();

    ImGui::End();

    // 编译刚结束时把红标记贴到行号上（鼠标停上去能看到那条报错）
    if (e.markersDirty) {
        e.markersDirty = false;
        refreshMarkers(e, th);
    }

    // ---- 键盘 ----
    if (ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        std::string err;
        if (dirty(e) && !saveFile(e, &err)) addOut(e, err, KindError);
    }
}

}  // namespace internal
}  // namespace easel
