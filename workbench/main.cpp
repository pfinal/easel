// 代码酷工作台 —— 学生的入口（D-29）
//
// 分工照抄 Processing，编辑器换成 VS Code：
//   学生在 VS Code 里写自己的两个文件（界面 src/app.cpp、逻辑 src/solver.cpp）；
//   工作台负责其余全部 —— 新建工程、编译、运行、停止、看报错、生成 exe、导出源码。
//   库、依赖、构建脚本由我们管，学生的 VS Code 里根本看不见它们。
//
// 关键一点：**作品是子进程**。所以「重新编译」永远不会撞上「正在运行的自己」——
// 这是 D-27 当初绕不过去的那个死结，换个进程模型就没了。
#include <easel/easel.h>

#include "internal.h"

#include <cstdio>
#include <cstring>
#include <iostream>

using namespace easel;
using namespace easel::internal;

namespace {

enum Stage { Idle, Configuring, Building, Running, Packing };

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

    // 「工程说清楚自己在做什么」：每次操作（编译并运行 / 生成 exe / 导出源码）从
    // 0.0s 重新起表，输出区里我们自己打的每一行都带 [ x.xs] 前缀；编译器/cmake/
    // 作品自己的原样输出不加前缀。opClock 在 beginLog() 里重置。
    Stopwatch            opClock;
    bool                 verboseUI = false;   // 输出区上方的「详细」勾选，同步 internal::verbose()

    // 新建工程对话框
    char                     newName[128] = "MySketch";
    char                     newParent[512] = {0};
    bool                     newTests = false;
    // 骨架下拉：0 空白 / 1 算法骨架 / 2+ 示例（exampleNames[newSkeleton - 2]，D-36）
    int                      newSkeleton = 0;
    std::vector<std::string> exampleNames;

    bool wantPackage = false;   // 「生成 exe」要多编一次 Release，分两步走
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

std::string configPath() { return joinPath(exeDir(), "workbench.json"); }

void loadConfig() {
    WB&  w = wb();
    json j = json::parse(fs::readText(configPath()), nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    if (j.contains("recent") && j["recent"].is_array())
        for (const json& x : j["recent"])
            if (x.is_string() && existsU8(x.get<std::string>())) w.recent.push_back(x);
    if (!w.recent.empty()) w.projectDir = w.recent.front();
}

void saveConfig() {
    WB&  w = wb();
    json j;
    j["recent"] = w.recent;
    fs::writeText(configPath(), j.dump(2));
}

void useProject(const std::string& dir) {
    WB& w = wb();
    if (dir.empty()) return;
    w.projectDir = dir;
    w.recent.erase(std::remove(w.recent.begin(), w.recent.end(), dir), w.recent.end());
    w.recent.insert(w.recent.begin(), dir);
    if (w.recent.size() > 8) w.recent.resize(8);
    saveConfig();
    w.status = "工程：" + baseName(dir);
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
// 环境细节。startConfigure()（平时编译）和 startPackage()（生成 exe 的 Release 配置）
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
    sayTS("第一次要把界面库编出来，几分钟。以后就只编你改的那几行了。");
    w.stage = Configuring;
    if (!spawn(argv, w.projectDir)) return;
    w.status = "配置中…";
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
        return;
    }
    sayTS("$ " + Proc::describe(argv) + "（作品的窗口会自己弹出来）");
    w.stage = Running;
    if (!spawn(argv, w.projectDir)) return;
    w.status = "运行中 —— 作品在另一个窗口里";
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
    beginLog();
    w.pending.clear();
    w.wantPackage = false;
    sayTS("工程 " + w.projectDir);
    if (cmakeMissing()) { flushLog(); return; }
    std::string build = buildDir(w.projectDir);
    if (existsU8(joinPath(build, "CMakeCache.txt"))) {
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

// 「打开日志目录」按钮：优先让 VS Code 打开日志文件本身（能直接看内容），
// 没有 VS Code 就退到系统的「在文件管理器里定位这个文件」命令——file::folder()
// 只会弹目录选择对话框，不是我们要的「带我去看那个文件」，不合适。
void openLogFolder(const std::string& logPath) {
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

// 「生成 exe」共用的部分：把 Release 版 exe + assets + data 拷成一个能双击的目录。
// GUI 的「生成 exe」按钮（finishPackage()）和无头 `--package`（headless::cmdPackage）
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

// 「生成 exe」：Release 编一遍，然后把 exe + assets + data 拷成一个能双击的目录
void finishPackage() {
    WB&         w = wb();
    std::string err;
    long long   bytes = 0;
    std::string dest = packageInto(w.projectDir, &err, &bytes);
    if (dest.empty()) {
        sayTS("生成失败：" + err, 1);
        flushLog();
        return;
    }
    sayTS("生成完成 → " + dest + "（" + formatBytes(bytes) + "）");
    flushLog();
    if (App::instance()) App::instance()->toast("exe 生成好了");
}

void startPackage() {
    WB& w = wb();
    if (w.projectDir.empty() || w.proc.running()) return;
    beginLog();
    w.wantPackage = true;
    sayTS("工程 " + w.projectDir);
    if (cmakeMissing()) {
        w.wantPackage = false;
        flushLog();
        return;
    }
    std::string              rel = releaseDir(w.projectDir);
    std::vector<std::string> argv = configureArgs(w.projectDir, rel, "Release");
    logConfigureStart(rel, "Release", argv);
    sayTS("生成交付用的 exe：优化过、不带调试信息，比平时慢一点编。");
    w.stage = Configuring;
    if (!spawn(argv, w.projectDir)) return;
    w.status = "配置 Release…";
    flushLog();
}

void pump() {
    WB& w = wb();
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
            w.status = "配置失败";
            w.stage = Idle;
            w.wantPackage = false;
            flushLog();
            return;
        }
        sayTS("配置完成");
        {
            std::string cfgText;
            for (const OutLine& o : w.out) cfgText += o.text + "\n";
            hintIfPrebuiltMismatch(cfgText);
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
            std::snprintf(buf, sizeof buf, "编译失败：%d 个错误、%d 个警告。点红色那行跳到代码里。",
                          w.errors, w.warnings);
            sayTS(buf, 1);
            w.status = "编译失败";
            w.stage = Idle;
            w.wantPackage = false;
            flushLog();
            return;
        }
        std::string exe = w.wantPackage ? exeIn(releaseDir(w.projectDir)) : appExe(w.projectDir);
        long long   sz = 0;
        fileStamp(exe, nullptr, &sz);
        std::snprintf(buf, sizeof buf, "编译完成 → %s（%s）%s", exe.c_str(), formatBytes(sz).c_str(),
                      w.warnings ? ("，" + std::to_string(w.warnings) + " 个警告").c_str() : "");
        sayTS(buf);
        if (w.wantPackage) {
            w.wantPackage = false;
            w.stage = Idle;
            w.status = "就绪";
            finishPackage();
        } else {
            startApp();
        }
        flushLog();
        return;
    }

    if (w.stage == Running) {
        if (code == 0) {
            sayTS("作品退出，退出码 0");
        } else if (code == -2) {
            sayTS("已停止");
        } else if (code >= 128) {
            std::snprintf(buf, sizeof buf,
                          "作品被信号 %d 打断 —— 多半是越界或空指针。改完再跑一次；"
                          "要查具体哪一行，用 VS Code 的 F5 调试。",
                          code - 128);
            sayTS(buf, 1);
        } else {
            std::snprintf(buf, sizeof buf, "作品退出，退出码 %d", code);
            sayTS(buf, 2);
        }
        w.stage = Idle;
        w.status = "就绪";
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
bool resolveProject(const std::string& raw, std::string* dir, std::string* err) {
    std::string d = raw.empty() ? std::string() : absPath(raw);
    if (d.empty() || !isDirU8(d)) {
        *err = "找不到工程目录：" + raw;
        return false;
    }
    if (!existsU8(joinPath(d, ".easel/CMakeLists.txt"))) {
        *err = d + " 不像一个工程（没有 .easel/CMakeLists.txt，是不是没用 --new 建的？）";
        return false;
    }
    if (toolchain().cmake.empty()) {
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

    bool needConfigure = !existsU8(joinPath(build, "CMakeCache.txt"));
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
        stamp("编译完成 → " + exe + "（" + formatBytes(sz) + "）" +
              (out.warnings ? "，" + std::to_string(out.warnings) + " 个警告" : std::string()));
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
    bool cacheExists = existsU8(joinPath(rel, "CMakeCache.txt"));
    if (cacheExists && easelDirStale(rel, wb().easelDir)) {
        stamp("Easel 位置变了，重新配置构建目录");
        cacheExists = false;
    }
    int cfgCode = cacheExists ? 0 : -1;
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
    else if (cfgCode == 0)
        stamp("编译失败：" + std::to_string(b.errors) + " 个错误、" + std::to_string(b.warnings) +
              " 个警告。用 --json 拿结构化诊断。");

    std::string output, packErr;
    long long   bytes = 0;
    if (b.ok) {
        output = packageInto(dir, &packErr, &bytes);
        if (output.empty()) b.ok = false;
    }
    if (!output.empty())
        stamp("生成完成 → " + output + "（" + formatBytes(bytes) + "）");
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
        stamp("导出完成 → " + rep.outDir + "（" + std::to_string(rep.files) + " 个文件，" +
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
            say("界面写在 src/app.cpp，算法写在 src/solver.cpp。改完回来点「编译并运行」。");
            ImGui::CloseCurrentPopup();
        } else {
            addOut(r.error, 1);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("取消", ImVec2(100 * dpi, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void draw(const Rect& r, const Theme& th, float dpi) {
    WB& w = wb();
    ImGui::SetNextWindowPos(ImVec2((float)r.x, (float)r.y));
    ImGui::SetNextWindowSize(ImVec2((float)r.w, (float)r.h));
    ImGui::Begin("##wb", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ---- 工程 ----
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
    ImGui::TextUnformatted(w.projectDir.empty() ? "还没有工程" : baseName(w.projectDir).c_str());
    ImGui::PopFont();
    if (!w.projectDir.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(th.muted.r, th.muted.g, th.muted.b, 1));
        ImGui::TextUnformatted(w.projectDir.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();

    bool busy = w.proc.running();
    if (ImGui::Button("新建工程…")) {
        if (!w.newParent[0])
            std::snprintf(w.newParent, sizeof w.newParent, "%s", defaultProjectsDir().c_str());
        ImGui::OpenPopup("新建工程");
    }
    ImGui::SameLine();
    if (ImGui::Button("打开工程…")) {
        std::string d = file::folder(w.projectDir.empty() ? nullptr : w.projectDir.c_str());
        if (!d.empty()) {
            if (existsU8(joinPath(d, "src/app.cpp")))
                useProject(d);
            else
                addOut(d + " 不像一个作品工程（里面没有 src/app.cpp）", 1);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(w.projectDir.empty());
    if (ImGui::Button("复制路径")) {
        ImGui::SetClipboardText(w.projectDir.c_str());
        say("工程路径已复制：" + w.projectDir);
    }
    ImGui::EndDisabled();
    if (!w.recent.empty()) {
        ImGui::SameLine();
        if (ImGui::BeginCombo("##recent", "最近的工程", ImGuiComboFlags_WidthFitPreview)) {
            for (const std::string& d : w.recent)
                if (ImGui::Selectable(d.c_str())) useProject(d);
            ImGui::EndCombo();
        }
    }
    drawNewProjectPopup(dpi);

    ImGui::Separator();
    ImGui::Spacing();

    // ---- 四个按钮 ----
    ImVec2 big(190 * dpi, 40 * dpi);
    ImGui::BeginDisabled(busy || w.projectDir.empty());
    if (ImGui::Button("编译并运行   F5", big)) compileAndRun();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!busy);
    if (ImGui::Button("停止", ImVec2(100 * dpi, 40 * dpi))) {
        w.proc.kill();
        say("已停止");
        w.stage = Idle;
        w.status = "就绪";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(busy || w.projectDir.empty());
    if (ImGui::Button("生成 exe", ImVec2(120 * dpi, 40 * dpi))) startPackage();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("编一个优化过的 Release 版，连同 assets / data 放进 dist/，可以直接双击");
    ImGui::SameLine();
    if (ImGui::Button("导出源码", ImVec2(120 * dpi, 40 * dpi))) {
        ExportOptions o;
        o.projectDir = w.projectDir;
        o.easelDir = w.easelDir;
        o.name = baseName(w.projectDir);
        std::string outDirShown = joinPath(joinPath(w.projectDir, "dist"), o.name);
        beginLog();
        sayTS("源工程 " + w.projectDir);
        sayTS("导出到 " + outDirShown);
        logVerboseEnv(w.projectDir);
        sayTS("正在导出完整源码…（几十 MB，界面会卡一下）");
        ExportReport rep = exportProject(o);
        for (const std::string& l : rep.checks)
            addOut(l, l.rfind("[×]", 0) == 0 ? 1 : l.rfind("[!]", 0) == 0 ? 2 : 0);
        if (rep.ok)
            sayTS("导出完成 → " + rep.outDir + "（" + std::to_string(rep.files) + " 个文件，" +
                  formatBytes(rep.bytes) + "）");
        else
            sayTS("导出失败：" + rep.error, 1);
        flushLog();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("导出一个完整工程：不装 Easel、不联网也能从头编出来");
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::SetNextItemWidth(380 * dpi);
    ImGui::InputTextWithHint("##args", "运行参数（可留空），例如 --open data/example.json --solve",
                             w.runArgs, sizeof w.runArgs);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", w.status.c_str());

    ImGui::Spacing();
    ImGui::Separator();

    // ---- 输出 ----
    if (ImGui::Checkbox("详细", &w.verboseUI)) internal::setVerbose(w.verboseUI);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("勾上之后下一次操作会多打一截排障细节：完整 PATH、子进程工作目录、\n"
                          "easel-prebuilt.json / VERSION.json 的原文。命令行等价于 --verbose");
    ImGui::SameLine();
    ImGui::TextUnformatted("输出");
    ImGui::SameLine();
    if (ImGui::SmallButton("清空")) {
        w.out.clear();
        w.errors = w.warnings = 0;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("复制全部")) {
        std::string all;
        for (const OutLine& o : w.out) all += o.text + "\n";
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(w.projectDir.empty());
    if (ImGui::SmallButton("打开日志目录")) openLogFolder(joinPath(w.projectDir, ".easel/last-build.log"));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("这次操作的完整输出存在 .easel/last-build.log 里");
    ImGui::EndDisabled();
    if (w.errors || w.warnings) {
        ImGui::SameLine();
        ImGui::TextColored(w.errors ? ImVec4(th.bad.r, th.bad.g, th.bad.b, 1)
                                    : ImVec4(th.warn.r, th.warn.g, th.warn.b, 1),
                           "%d 错误 · %d 警告", w.errors, w.warnings);
    }

    ImGui::BeginChild("##out", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushFont(monoFont(), ImGui::GetStyle().FontSizeBase * 0.92f);
    for (size_t i = 0; i < w.out.size(); ++i) {
        const OutLine& o = w.out[i];
        ImVec4         c = o.kind == 1   ? ImVec4(th.bad.r, th.bad.g, th.bad.b, 1)
                           : o.kind == 2 ? ImVec4(th.warn.r, th.warn.g, th.warn.b, 1)
                           : o.kind == 3 ? ImVec4(th.accent.r, th.accent.g, th.accent.b, 1)
                                         : ImVec4(th.fg.r, th.fg.g, th.fg.b, 1);
        ImGui::PushStyleColor(ImGuiCol_Text, c);
        if (!o.file.empty() && o.line > 0) {
            ImGui::PushID((int)i);
            if (ImGui::Selectable(o.text.c_str())) {
                if (!openInEditor(o.file, o.line, o.col)) {
                    std::string loc = o.file + ":" + std::to_string(o.line);
                    ImGui::SetClipboardText(loc.c_str());
                    addOut("没找到 VS Code，位置已复制：" + loc, 2);
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PopID();
        } else {
            ImGui::TextUnformatted(o.text.c_str());
        }
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.f) ImGui::SetScrollHereY(1.f);
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace

int main(int argc, char** argv) {
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
    app.title("Easel").size(1100, 720).theme(Theme::Forest()).editorEnabled(false)
       .debugConsoleEnabled(false)    // 工具类程序不需要调试台
       .idleThrottle(true)            // 界面静止的时候（没人点、也没有子进程在编译/跑）降到 10 帧省电
       .busyWhen([]{ return wb().proc.running(); });   // 编译/运行中也保持满帧
    app.statusBar(true);
    internal::setVerbose(cli::args().has("verbose"));   // --verbose；GUI 的「详细」勾选也改它

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
        if (cli::args().has("example"))
            o.exampleDir = joinPath(joinPath(w.easelDir, "examples"), cli::args().str("example"));
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
                       cli::args().has("package") || cli::args().has("export");
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

    if (!cli::args().at(0).empty()) useProject(absPath(cli::args().at(0)));

    // 工作台一启动就打启动横幅，在输出区最上面——查问题时第一眼就看得到用的是哪个
    // Easel、哪份预编译、编译器/cmake/ninja 在哪儿（D-新，第 1 部分）。
    w.verboseUI = internal::verbose();
    beginLog();
    w.status = w.projectDir.empty() ? "先新建一个工程，或者打开一个已有的" : "工程：" + baseName(w.projectDir);

    app.onFrame([](double) { pump(); });
    app.onWindow([&app](const Rect& r) { draw(r, app.theme(), (float)app.dpiScale()); });
    app.onKey([](int key) {
        if (key == ImGuiKey_F5) compileAndRun();
    });
    app.status("Easel");
    return app.run();
}
