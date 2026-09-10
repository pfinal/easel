// Easel — toolchain.cpp  「运行」按钮要用的那个 g++ 到底在哪
//
// 学生双击 app.exe 的时候，PATH 里是没有 w64devkit 的（那是 start.bat 干的活）。
// 所以不能只查 PATH：还要顺着 exe 往上找工具箱。找不到就明说下一步怎么办，
// 别让学生对着一个灰按钮发呆。
#include "internal.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#    include <windows.h>
#else
#    include <unistd.h>
#    if defined(__APPLE__)
#        include <mach-o/dyld.h>
#    endif
#endif

namespace easel {
namespace internal {

namespace {

#if defined(_WIN32)
const char kSep = '\\';
#else
const char kSep = '/';
#endif

std::vector<std::string> splitPathEnv() {
    std::vector<std::string> out;
    const char*              p = std::getenv("PATH");
    if (!p) return out;
    std::string s = p, cur;
#if defined(_WIN32)
    const char delim = ';';
#else
    const char delim = ':';
#endif
    for (char c : s) {
        if (c == delim) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool isFile(const std::string& p) {
    long long m = 0, sz = 0;
    return fileStamp(p, &m, &sz);
}

// 在一串目录里找第一个存在的可执行文件
std::string lookIn(const std::vector<std::string>& dirs, const char* const* names) {
    for (const std::string& d : dirs)
        for (const char* const* n = names; *n; ++n) {
            std::string cand = joinPath(d, *n);
            if (isFile(cand)) return cand;
        }
    return {};
}

const char* const kCxxNames[] = {
#if defined(_WIN32)
    "g++.exe", "clang++.exe",
#else
    "g++", "clang++",
#endif
    nullptr};

Toolchain g_tc;
bool      g_found = false;

}  // namespace

// ---------------------------------------------------------------- 路径小工具
std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char last = a[a.size() - 1];
    if (last == '/' || last == '\\') return a + b;
    return a + kSep + b;
}

std::string baseName(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

std::string absPath(const std::string& path) {
    if (path.empty()) return path;
#if defined(_WIN32)
    if (path.size() > 1 && (path[1] == ':' || (path[0] == '\\' && path[1] == '\\'))) return path;
#else
    if (path[0] == '/') return path;
#endif
    return joinPath(fs::cwd(), path);
}

std::string exePath() {
#if defined(_WIN32)
    std::wstring buf(32768, L'\0');
    DWORD        n = GetModuleFileNameW(nullptr, &buf[0], (DWORD)buf.size());
    if (n == 0) return {};
    buf.resize(n);
    return narrow(buf);
#elif defined(__APPLE__)
    std::uint32_t n = 0;
    _NSGetExecutablePath(nullptr, &n);
    std::string buf(n, '\0');
    if (_NSGetExecutablePath(&buf[0], &n) != 0) return {};
    buf.resize(std::strlen(buf.c_str()));
    return buf;
#else
    char    buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return {};
    buf[n] = '\0';
    return buf;
#endif
}

std::string exeDir() { return fs::dirOf(exePath()); }

// ---------------------------------------------------------------- 找编译器
namespace {

// 找 cxx。找到就顺便记下工具箱根目录（cmake / ninja 也在那儿）。
void findCxx() {
    if (const char* env = std::getenv("EASEL_CXX")) {
        if (*env && isFile(env)) {
            g_tc.cxx = env;
            g_tc.source = "环境变量 EASEL_CXX";
            return;
        }
    }

    // 绿色工具箱：从 exe 往上翻几层（学生双击 exe 时 PATH 里没有它，这一步就是为这个写的）
    std::string dir = exeDir();
    for (int up = 0; up < 6 && !dir.empty(); ++up) {
        for (const char* kit : {"w64devkit", "mingw64", "mingw32"}) {
            std::string cand = lookIn({joinPath(joinPath(dir, kit), "bin")}, kCxxNames);
            if (!cand.empty()) {
                g_tc.cxx = cand;
                g_tc.kit = dir;
                g_tc.source = "工具箱（" + dir + "）";
                return;
            }
        }
        std::string parent = fs::dirOf(dir);
        if (parent == dir) break;
        dir = parent;
    }

    std::string cand = lookIn(splitPathEnv(), kCxxNames);
    if (!cand.empty()) {
        g_tc.cxx = cand;
        g_tc.source = "PATH";
        return;
    }

    const char* const kFixed[] = {
#if defined(_WIN32)
        "C:/w64devkit/bin/g++.exe", "C:/msys64/mingw64/bin/g++.exe", "C:/MinGW/bin/g++.exe",
#else
        "/usr/bin/g++", "/usr/bin/clang++", "/usr/local/bin/g++", "/opt/homebrew/bin/g++",
#endif
        nullptr};
    for (const char* const* p = kFixed; *p; ++p) {
        if (isFile(*p)) {
            g_tc.cxx = *p;
            g_tc.source = "系统固定位置";
            return;
        }
    }

    g_tc.note =
#if defined(_WIN32)
        "没找到 g++。用工具箱里的 start.bat 打开这个程序，或者把 w64devkit\\bin 加进 PATH。";
#else
        "没找到 g++。Mac 上跑一次 xcode-select --install；Linux 上装 g++。";
#endif
}

// cmake / ninja / code：先看工具箱，再看 PATH，最后几个老地方
void findRest() {
    const char* const kCmake[] = {
#if defined(_WIN32)
        "cmake.exe",
#else
        "cmake",
#endif
        nullptr};
    const char* const kNinja[] = {
#if defined(_WIN32)
        "ninja.exe",
#else
        "ninja",
#endif
        nullptr};
    const char* const kCode[] = {
#if defined(_WIN32)
        "code.cmd", "code.exe", "code",
#else
        "code",
#endif
        nullptr};

    std::vector<std::string> kitDirs;
    if (!g_tc.kit.empty()) {
        kitDirs.push_back(joinPath(joinPath(g_tc.kit, "cmake"), "bin"));
        kitDirs.push_back(joinPath(g_tc.kit, "ninja"));
        kitDirs.push_back(joinPath(g_tc.kit, "VSCode"));            // 便携版 VS Code
        kitDirs.push_back(joinPath(joinPath(g_tc.kit, "VSCode"), "bin"));
    }
    std::vector<std::string> path = splitPathEnv();
    std::vector<std::string> all = kitDirs;
    all.insert(all.end(), path.begin(), path.end());

    g_tc.cmake = lookIn(all, kCmake);
    g_tc.ninja = lookIn(all, kNinja);
    g_tc.vscode = lookIn(all, kCode);
#if defined(__APPLE__)
    if (g_tc.cmake.empty())
        for (const char* p : {"/usr/local/bin/cmake", "/opt/homebrew/bin/cmake",
                              "/Applications/CMake.app/Contents/bin/cmake"})
            if (isFile(p)) { g_tc.cmake = p; break; }
    if (g_tc.vscode.empty()) {
        const char* p = "/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code";
        if (isFile(p)) g_tc.vscode = p;
    }
#endif

    // 起 cmake 之前要把这些目录插进 PATH：cmake 自己要能找到 g++ 和 ninja
    for (const std::string& d : {fs::dirOf(g_tc.cxx), fs::dirOf(g_tc.cmake), fs::dirOf(g_tc.ninja)})
        if (!d.empty()) g_tc.binDirs.push_back(d);
}

}  // namespace

const Toolchain& toolchain(bool refresh) {
    if (g_found && !refresh) return g_tc;
    g_found = true;
    g_tc = Toolchain{};
    findCxx();
    // 同一套里的 C 编译器：cmake 配置时要 CMAKE_C_COMPILER
    if (!g_tc.cxx.empty()) {
        std::string cc = g_tc.cxx;
        size_t      at = cc.rfind("g++");
        if (at != std::string::npos) cc.replace(at, 3, "gcc");
        else if ((at = cc.rfind("clang++")) != std::string::npos) cc.replace(at, 7, "clang");
        if (isFile(cc)) g_tc.cc = cc;
    }
    findRest();
    return g_tc;
}

// 点一下报错行 → 让 VS Code 跳到那一行。没有 VS Code 就返回 false（调用方改成复制路径）。
bool openInEditor(const std::string& file, int line, int col) {
    const Toolchain& tc = toolchain();
    if (tc.vscode.empty() || file.empty()) return false;
    std::string target = file;
    if (line > 0) {
        target += ":" + std::to_string(line);
        if (col > 0) target += ":" + std::to_string(col);
    }
    std::vector<std::string> argv;
    // Windows 上 code 是个 .cmd，CreateProcess 起不了批处理，得让 cmd.exe 代劳
    std::string low = tc.vscode;
    for (char& c : low) c = (char)std::tolower((unsigned char)c);
    if (low.size() > 4 && (low.substr(low.size() - 4) == ".cmd" || low.substr(low.size() - 4) == ".bat"))
        argv = {"cmd.exe", "/c", tc.vscode, "-g", target};
    else
        argv = {tc.vscode, "-g", target};

    // 起了就不管了：code 自己会退出。放个静态的 Proc，下一次调用会把上一个收走。
    static Proc p;
    std::string err;
    return p.start(argv, {}, &err);
}

// 取版本号：只在自检和「重新找编译器」时调，会阻塞最多 2 秒
std::string toolchainVersion() {
    const Toolchain& tc = toolchain();
    if (tc.cxx.empty()) return {};
    std::string out;
    int         code = -1;
    if (!runBlocking({tc.cxx, "--version"}, {}, 2000, &out, &code)) return {};
    size_t nl = out.find('\n');
    return nl == std::string::npos ? out : out.substr(0, nl);
}

}  // namespace internal
}  // namespace easel
