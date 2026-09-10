// Easel — log_capture.cpp
// 把 stdout / stderr 接到应用内的日志窗（D-23 第 3 条）。
// 顺带解决 Windows GUI 子系统吞掉 printf 的老问题：同一份 solver.cpp，
// 在终端里跑能看到输出，在 App 里跑也能看到。
#include "internal.h"

#if defined(_WIN32)
#  include <fcntl.h>
#  include <io.h>
#  include <windows.h>
#  define EASEL_DUP _dup
#  define EASEL_DUP2 _dup2
#  define EASEL_CLOSE _close
#  define EASEL_READ _read
#  define EASEL_WRITE _write
#else
#  include <fcntl.h>
#  include <unistd.h>
#  define EASEL_DUP dup
#  define EASEL_DUP2 dup2
#  define EASEL_CLOSE close
#  define EASEL_READ read
#  define EASEL_WRITE write
#endif

namespace easel {
namespace internal {

// ============================================================ Shared
Shared& shared() {
    static Shared s;
    return s;
}

void Shared::addLog(LogLevel lv, const char* file, int line, const std::string& msg) {
    if (!log.empty()) {
        LogEntry& last = log.back();
        if (last.level == lv && last.line == line && last.msg == msg &&
            last.file == (file ? file : "")) {
            ++last.repeat;   // 洪水折叠：一样的话就累加，不再刷屏
            last.frame = frame;
            return;
        }
    }
    LogEntry e;
    e.level = lv;
    e.file = file ? file : "";
    e.line = line;
    e.msg = msg;
    e.frame = frame;
    log.push_back(std::move(e));
    while (log.size() > logLimit) {
        log.pop_front();
        ++droppedLogs;
    }
}

void Shared::addTrace(const char* name, double value, long long index) {
    for (TraceSeries& s : traces) {
        if (s.name == name) {
            s.xs.push_back((double)index);
            s.ys.push_back(value);
            if (s.xs.size() > traceLimit) {
                s.xs.erase(s.xs.begin(), s.xs.begin() + (long)(s.xs.size() - traceLimit));
                s.ys.erase(s.ys.begin(), s.ys.begin() + (long)(s.ys.size() - traceLimit));
            }
            return;
        }
    }
    TraceSeries s;
    s.name = name;
    s.xs.push_back((double)index);
    s.ys.push_back(value);
    traces.push_back(std::move(s));
}

void Shared::addDbg(const char* key, const char* value) {
    for (DbgValue& d : dbgs) {
        if (d.key == key) {
            d.value = value;
            d.frame = frame;
            char*  endp = nullptr;
            double v = std::strtod(value, &endp);
            if (endp && endp != value && *endp == '\0') {
                d.numeric = true;
                d.history.push_back(v);
                if (d.history.size() > 600) d.history.erase(d.history.begin());
            }
            return;
        }
    }
    DbgValue d;
    d.key = key;
    d.value = value;
    d.frame = frame;
    char*  endp = nullptr;
    double v = std::strtod(value, &endp);
    if (endp && endp != value && *endp == '\0') {
        d.numeric = true;
        d.history.push_back(v);
    }
    dbgs.push_back(std::move(d));
}

// ============================================================ stdout 接管
namespace {
int         g_savedOut = -1;
int         g_savedErr = -1;
int         g_pipeRead = -1;
bool        g_active = false;
std::string g_partial;

void emitLine(const std::string& line) {
    if (line.empty()) return;
    shared().addLog(LogLevel::Info, "cout", 0, line);
}
}  // namespace

bool captureActive() { return g_active; }

void writeThrough(const char* s, size_t n) {
    // 接管之前：直接写 fd 1。接管之后：写那份存下来的真 stdout。
    // Windows 的 GUI 子系统里可能根本没有 stdout（g_savedOut < 0），
    // 这时候什么都别写 —— 写回 fd 1 就是写回管道，自己喂自己，会死循环。
    int fd = g_active ? g_savedOut : 1;
    if (fd >= 0 && n) { auto r = EASEL_WRITE(fd, s, (unsigned)n); (void)r; }
}

void startCapture() {
    if (g_active) return;
    int fds[2];
#if defined(_WIN32)
    if (_pipe(fds, 1 << 16, _O_BINARY) != 0) return;
#else
    if (pipe(fds) != 0) return;
#endif
    std::fflush(stdout);
    std::fflush(stderr);
    g_savedOut = EASEL_DUP(1);
    g_savedErr = EASEL_DUP(2);
    if (g_savedOut < 0) {
        // Windows 的 GUI 子系统里本来就没有 stdout —— 那就更该接管了
        g_savedOut = -1;
    }
    EASEL_DUP2(fds[1], 1);
    EASEL_DUP2(fds[1], 2);
    EASEL_CLOSE(fds[1]);
    g_pipeRead = fds[0];
#if !defined(_WIN32)
    int fl = fcntl(g_pipeRead, F_GETFL, 0);
    fcntl(g_pipeRead, F_SETFL, fl | O_NONBLOCK);
#endif
    // 管道默认是全缓冲的，不设成无缓冲就要等 4KB 才看得到输出
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    g_active = true;
}

void pumpCapture() {
    if (!g_active || g_pipeRead < 0) return;
    char buf[8192];
    for (int guard = 0; guard < 64; ++guard) {
#if defined(_WIN32)
        HANDLE h = (HANDLE)_get_osfhandle(g_pipeRead);
        DWORD  avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
        int n = EASEL_READ(g_pipeRead, buf, (unsigned)std::min<DWORD>(avail, sizeof buf));
#else
        int n = (int)EASEL_READ(g_pipeRead, buf, sizeof buf);
#endif
        if (n <= 0) break;
        writeThrough(buf, (size_t)n);   // 终端里也要看得到
        g_partial.append(buf, (size_t)n);
        size_t start = 0, nl;
        while ((nl = g_partial.find('\n', start)) != std::string::npos) {
            std::string line = g_partial.substr(start, nl - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            emitLine(line);
            start = nl + 1;
        }
        g_partial.erase(0, start);
        if (g_partial.size() > 65536) {   // 一直不换行的话也别无限攒着
            emitLine(g_partial);
            g_partial.clear();
        }
        if (n < (int)sizeof buf) break;
    }
}

void stopCapture() {
    if (!g_active) return;
    std::fflush(stdout);
    std::fflush(stderr);
    pumpCapture();
    if (!g_partial.empty()) { emitLine(g_partial); g_partial.clear(); }
    if (g_savedOut >= 0) EASEL_DUP2(g_savedOut, 1);
    if (g_savedErr >= 0) EASEL_DUP2(g_savedErr, 2);
    if (g_pipeRead >= 0) EASEL_CLOSE(g_pipeRead);
    if (g_savedOut >= 0) EASEL_CLOSE(g_savedOut);
    if (g_savedErr >= 0) EASEL_CLOSE(g_savedErr);
    g_pipeRead = g_savedOut = g_savedErr = -1;
    g_active = false;
}

// ============================================================ 钩子
namespace {

void hookLog(LogLevel level, const char* file, int line, const char* msg) {
    Shared& s = shared();
    s.addLog(level, detail::shortFile(file), line, msg ? msg : "");
    // 终端那边也要有一份（学生可能是从命令行启动的 App）
    char buf[2200];
    int  n = std::snprintf(buf, sizeof buf, "[%s] %s:%d  %s\n", levelName(level),
                           detail::shortFile(file), line, msg ? msg : "");
    if (n > 0) writeThrough(buf, (size_t)std::min<size_t>((size_t)n, sizeof buf - 1));
}

void hookTrace(const char* name, double value, long long index) {
    shared().addTrace(name, value, index);
}

void hookCheck(const char* expr, const char* msg, const char* file, int line) {
    Shared& s = shared();
    ++s.checkFailures;
    s.banner = true;
    s.bannerExpr = expr ? expr : "";
    s.bannerMsg = msg ? msg : "";
    s.bannerWhere = std::string(detail::shortFile(file)) + ":" + std::to_string(line);
    s.addLog(LogLevel::Check, detail::shortFile(file), line,
             std::string(expr ? expr : "") + "  ——  " + (msg ? msg : ""));
    char buf[2200];
    int  n = std::snprintf(buf, sizeof buf, "[CHECK] %s:%d  %s  ——  %s\n", detail::shortFile(file),
                           line, expr ? expr : "", msg ? msg : "");
    if (n > 0) writeThrough(buf, (size_t)std::min<size_t>((size_t)n, sizeof buf - 1));
}

void hookDbg(const char* key, const char* value) { shared().addDbg(key, value); }

void hookCrash(const char* what, const char* stack) {
    Shared& s = shared();
    s.crashed = true;
    s.crashWhat = what ? what : "";
    s.crashStack = stack ? stack : "";
}

}  // namespace

void installHooks(bool withCheck) {
    hooks::log = &hookLog;
    hooks::trace = &hookTrace;
    hooks::check = withCheck ? &hookCheck : nullptr;
    hooks::dbg = &hookDbg;
    hooks::crash = &hookCrash;
}

void removeHooks() {
    hooks::log = nullptr;
    hooks::trace = nullptr;
    hooks::check = nullptr;
    hooks::dbg = nullptr;
    hooks::crash = nullptr;
}

}  // namespace internal
}  // namespace easel
