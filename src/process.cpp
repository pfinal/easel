// Easel — process.cpp  起一个子进程，非阻塞地把它的输出捞回来
//
// 编辑栏的「编译并运行」用它（D-28）。三条规矩：
//   1. 不开线程（D-11）。每帧 pump 一次，管子里有多少读多少，界面永远不卡。
//   2. 不经过 shell。参数是数组，不拼字符串 —— 学生的路径里有空格、有中文都不怕。
//   3. 输出当 UTF-8 原样收下。gcc 的诊断和 -fexec-charset 默认输出都是 UTF-8，
//      直接就能进 ImGui；换成 cmd.exe 转一手反而会变成 GBK。
#include "internal.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>

#include <sys/stat.h>

#if defined(_WIN32)
#    include <direct.h>
#    include <windows.h>
#else
#    include <errno.h>
#    include <fcntl.h>
#    include <signal.h>
#    include <spawn.h>
#    include <dirent.h>
#    include <sys/wait.h>
#    include <unistd.h>
extern char** environ;
// posix_spawn 的 chdir：macOS 10.15+ / glibc 2.29+ 才有。没有就退回父进程 chdir 再改回来
// （单线程 GUI，改一下再改回来是安全的）。
#    if defined(__APPLE__) || (defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29)))
#        define EASEL_SPAWN_CHDIR 1
#    endif
#endif

namespace easel {
namespace internal {

#if defined(_WIN32)

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

namespace {
// Windows 把命令行当一整个字符串传，转义规则见 CommandLineToArgvW 的文档
std::wstring quoteArg(const std::string& a) {
    std::wstring w = widen(a);
    if (!w.empty() && w.find_first_of(L" \t\"") == std::wstring::npos) return w;
    std::wstring out = L"\"";
    for (size_t i = 0; i < w.size(); ++i) {
        size_t slashes = 0;
        while (i < w.size() && w[i] == L'\\') { ++slashes; ++i; }
        if (i == w.size()) {
            out.append(slashes * 2, L'\\');
            break;
        }
        if (w[i] == L'"') {
            out.append(slashes * 2 + 1, L'\\');
        } else {
            out.append(slashes, L'\\');
        }
        out.push_back(w[i]);
    }
    out.push_back(L'"');
    return out;
}
}  // namespace

#endif  // _WIN32

// ---------------------------------------------------------------- 文件（中文路径安全）
std::FILE* fopenU8(const std::string& path, const char* mode) {
#if defined(_WIN32)
    return _wfopen(widen(path).c_str(), widen(mode).c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

bool readTextU8(const std::string& path, std::string* out) {
    std::FILE* f = fopenU8(path, "rb");
    if (!f) return false;
    out->clear();
    char   buf[16384];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out->append(buf, n);
    std::fclose(f);
    return true;
}

bool writeTextU8(const std::string& path, const std::string& text) {
    std::string d = fs::dirOf(path);
    if (!d.empty()) makeDirsU8(d);   // 和 core.h 的 fs::writeText 一样，缺目录就建
    std::FILE* f = fopenU8(path, "wb");
    if (!f) return false;
    size_t n = text.empty() ? 0 : std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    return n == text.size();
}

// 文件的时间戳与大小 —— 编辑栏靠它发现「这个文件在外面被改过」
bool fileStamp(const std::string& path, long long* mtime, long long* size) {
#if defined(_WIN32)
    struct _stat64 st;
    if (_wstat64(widen(path).c_str(), &st) != 0) return false;
#else
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return false;
#endif
    if (mtime) *mtime = (long long)st.st_mtime;
    if (size) *size = (long long)st.st_size;
    return true;
}

bool existsU8(const std::string& path);

// 建目录（逐级）。core.h 的 fs::makeDirs 是窄字符的，Windows 上中文目录会失败。
bool makeDirsU8(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/' || path[i] == '\\') {
            if (!cur.empty() && cur != "." && cur != ".." && !existsU8(cur)) {
#if defined(_WIN32)
                if (cur.size() == 2 && cur[1] == ':') { /* 盘符本身，跳过 */ }
                else _wmkdir(widen(cur).c_str());
#else
                ::mkdir(cur.c_str(), 0755);
#endif
            }
            if (i == path.size()) break;
        }
        cur.push_back(path[i]);
    }
    return existsU8(path);
}

bool existsU8(const std::string& path) {
#if defined(_WIN32)
    return GetFileAttributesW(widen(path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    return ::access(path.c_str(), F_OK) == 0;
#endif
}

bool isDirU8(const std::string& path) {
#if defined(_WIN32)
    DWORD a = GetFileAttributesW(widen(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

// 列一层目录（不递归）。名字是 UTF-8。
std::vector<DirEntry> listDirU8(const std::string& dir) {
    std::vector<DirEntry> out;
#if defined(_WIN32)
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(widen(dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::string name = narrow(fd.cFileName);
        if (name == "." || name == "..") continue;
        out.push_back({name, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
                       ((long long)fd.nFileSizeHigh << 32) | (long long)fd.nFileSizeLow});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent* e = ::readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string full = dir + "/" + name;
        struct stat st;
        if (::stat(full.c_str(), &st) != 0) continue;
        out.push_back({name, S_ISDIR(st.st_mode) != 0, (long long)st.st_size});
    }
    ::closedir(d);
#endif
    return out;
}

// 拷一个文件（二进制原样）。
bool copyFileU8(const std::string& from, const std::string& to) {
    std::FILE* in = fopenU8(from, "rb");
    if (!in) return false;
    makeDirsU8(fs::dirOf(to));
    std::FILE* out = fopenU8(to, "wb");
    if (!out) { std::fclose(in); return false; }
    char   buf[65536];
    size_t n;
    bool   ok = true;
    while ((n = std::fread(buf, 1, sizeof buf, in)) > 0)
        if (std::fwrite(buf, 1, n, out) != n) { ok = false; break; }
    std::fclose(in);
    std::fclose(out);
    return ok;
}

// 删掉一整棵目录（递归）。测试的临时目录连跑多次前先自己清场就靠它；
// 目标本来就不存在算成功（幂等，调用方不用先判断 existsU8）。
bool removeTreeU8(const std::string& path) {
    if (!existsU8(path)) return true;
    if (!isDirU8(path)) {
#if defined(_WIN32)
        return _wremove(widen(path).c_str()) == 0;
#else
        return ::remove(path.c_str()) == 0;
#endif
    }
    bool ok = true;
    for (const DirEntry& e : listDirU8(path)) ok = removeTreeU8(joinPath(path, e.name)) && ok;
#if defined(_WIN32)
    return ok && _wrmdir(widen(path).c_str()) == 0;
#else
    return ok && ::rmdir(path.c_str()) == 0;
#endif
}

// ---------------------------------------------------------------- Proc
struct Proc::Impl {
#if defined(_WIN32)
    HANDLE proc = nullptr;
    HANDLE rd = nullptr;
#else
    pid_t pid = -1;
    int   rd = -1;
#endif
};

Proc::Proc() : p_(new Impl) {}
Proc::~Proc() {
    kill();
    reap();
    delete p_;
}

std::string Proc::describe(const std::vector<std::string>& argv) {
    std::string s;
    for (size_t i = 0; i < argv.size(); ++i) {
        bool q = argv[i].find(' ') != std::string::npos;
        if (i) s += ' ';
        if (q) s += '"';
        s += argv[i];
        if (q) s += '"';
    }
    return s;
}

void Proc::reap() {
#if defined(_WIN32)
    if (p_->rd) { CloseHandle(p_->rd); p_->rd = nullptr; }
    if (p_->proc) { CloseHandle(p_->proc); p_->proc = nullptr; }
#else
    if (p_->rd >= 0) { ::close(p_->rd); p_->rd = -1; }
    if (p_->pid > 0) { int st = 0; ::waitpid(p_->pid, &st, WNOHANG); p_->pid = -1; }
#endif
}

namespace {
// 临时把几个目录插进本进程的 PATH，起完子进程就还原。
// 子进程继承的是这一刻的环境 —— 学生双击 exe 时 PATH 里没有工具箱，cmake 就是靠这个
// 才找得到 g++ 和 ninja。单线程 GUI，改一下再改回来是安全的。
class PathGuard {
public:
    explicit PathGuard(const std::vector<std::string>* dirs) {
        if (!dirs || dirs->empty()) return;
        const char* cur = std::getenv("PATH");
        old_ = cur ? cur : "";
        active_ = true;
#if defined(_WIN32)
        const char sep = ';';
#else
        const char sep = ':';
#endif
        std::string next;
        for (const std::string& d : *dirs) {
            if (d.empty()) continue;
            if (old_.find(d) != std::string::npos) continue;
            next += d;
            next += sep;
        }
        next += old_;
        setPath(next);
    }
    ~PathGuard() {
        if (active_) setPath(old_);
    }

private:
    static void setPath(const std::string& v) {
#if defined(_WIN32)
        SetEnvironmentVariableW(L"PATH", widen(v).c_str());
        _putenv_s("PATH", v.c_str());
#else
        ::setenv("PATH", v.c_str(), 1);
#endif
    }
    std::string old_;
    bool        active_ = false;
};
}  // namespace

bool Proc::start(const std::vector<std::string>& argv, const std::string& workDir,
                 std::string* error, const std::vector<std::string>* pathPrepend) {
    PathGuard guard(pathPrepend);
    if (argv.empty()) { if (error) *error = "没有要执行的命令"; return false; }
    kill();
    reap();
    running_ = false;
    exitCode_ = -1;
    cmd_ = describe(argv);

#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 1 << 16)) {
        if (error) *error = "CreatePipe 失败";
        return false;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    // 子进程的 stdin 接到 NUL：学生的程序里若有 cin，不至于永远卡住等输入
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, nullptr);

    std::wstring cmdline;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) cmdline += L' ';
        cmdline += quoteArg(argv[i]);
    }
    std::wstring cwdw = widen(workDir);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = (nul == INVALID_HANDLE_VALUE) ? nullptr : nul;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmdline.begin(), cmdline.end());
    mutableCmd.push_back(L'\0');
    BOOL ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr,
                             cwdw.empty() ? nullptr : cwdw.c_str(), &si, &pi);
    CloseHandle(wr);
    if (nul && nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(rd);
        if (error) *error = "起不了进程（错误码 " + std::to_string((long long)GetLastError()) + "）：" + argv[0];
        return false;
    }
    CloseHandle(pi.hThread);
    p_->proc = pi.hProcess;
    p_->rd = rd;
#else
    int fds[2];
    if (::pipe(fds) != 0) { if (error) *error = "pipe 失败"; return false; }

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    posix_spawn_file_actions_addclose(&fa, fds[1]);
#if defined(EASEL_SPAWN_CHDIR)
    if (!workDir.empty()) posix_spawn_file_actions_addchdir_np(&fa, workDir.c_str());
    std::string saveCwd;
#else
    std::string saveCwd = workDir.empty() ? std::string() : fs::cwd();
    if (!workDir.empty() && ::chdir(workDir.c_str()) != 0) {
        posix_spawn_file_actions_destroy(&fa);
        ::close(fds[0]); ::close(fds[1]);
        if (error) *error = "进不去目录：" + workDir;
        return false;
    }
#endif

    std::vector<char*> cargv;
    for (const std::string& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    pid_t pid = -1;
    int   rc = ::posix_spawnp(&pid, argv[0].c_str(), &fa, nullptr, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (!saveCwd.empty()) { if (::chdir(saveCwd.c_str()) != 0) { /* 回不去就算了 */ } }
    ::close(fds[1]);
    if (rc != 0) {
        ::close(fds[0]);
        if (error) *error = std::string("起不了进程：") + argv[0] + "（" + std::strerror(rc) + "）";
        return false;
    }
    ::fcntl(fds[0], F_SETFL, O_NONBLOCK);
    p_->pid = pid;
    p_->rd = fds[0];
#endif
    running_ = true;
    bytes_ = 0;
    return true;
}

bool Proc::pump(std::string* appended) {
    if (!running_) return false;
    char buf[8192];
    bool eof = false;

    for (int guard = 0; guard < 64; ++guard) {   // 一帧最多捞 512 KB，不让界面卡住
#if defined(_WIN32)
        DWORD avail = 0;
        if (!PeekNamedPipe(p_->rd, nullptr, 0, nullptr, &avail, nullptr)) { eof = true; break; }
        if (avail == 0) break;
        DWORD got = 0;
        if (!ReadFile(p_->rd, buf, (DWORD)std::min<size_t>(sizeof buf, avail), &got, nullptr) || got == 0) {
            eof = true;
            break;
        }
        if (appended) appended->append(buf, got);
        bytes_ += (long long)got;
#else
        ssize_t got = ::read(p_->rd, buf, sizeof buf);
        if (got > 0) {
            if (appended) appended->append(buf, (size_t)got);
            bytes_ += got;
        } else if (got == 0) {
            eof = true;
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            eof = true;
            break;
        }
#endif
        if (bytes_ > kMaxOutput) {
            if (appended) *appended += "\n[Easel] 输出太多了（超过 8 MB），已经把它停掉 —— "
                                       "是不是死循环里在打印？\n";
            kill();
            return false;
        }
    }

    // 进程还在不在
#if defined(_WIN32)
    DWORD code = 0;
    if (GetExitCodeProcess(p_->proc, &code) && code != STILL_ACTIVE) {
        if (!eof) return true;   // 人没了但管子里可能还有货，下一帧再收
        exitCode_ = (int)code;
        running_ = false;
        reap();
    }
#else
    int   st = 0;
    pid_t r = ::waitpid(p_->pid, &st, WNOHANG);
    if (r == p_->pid) {
        if (!eof) {   // 先把管子抽干
            char more[8192];
            ssize_t got;
            while ((got = ::read(p_->rd, more, sizeof more)) > 0)
                if (appended) appended->append(more, (size_t)got);
        }
        exitCode_ = WIFEXITED(st) ? WEXITSTATUS(st) : (WIFSIGNALED(st) ? 128 + WTERMSIG(st) : -1);
        p_->pid = -1;
        running_ = false;
        reap();
    }
#endif
    return running_;
}

void Proc::kill() {
    if (!running_) return;
#if defined(_WIN32)
    if (p_->proc) TerminateProcess(p_->proc, 1);
#else
    if (p_->pid > 0) ::kill(p_->pid, SIGKILL);
#endif
    running_ = false;
    exitCode_ = -2;
    reap();
}

// 一次性的活（查版本、试编一下）用这个：起进程 → 等它结束 → 把输出给你。
// 每帧要跑的东西不许用它。
bool runBlocking(const std::vector<std::string>& argv, const std::string& workDir, int timeoutMs,
                 std::string* out, int* exitCode, const std::vector<std::string>* pathPrepend) {
    Proc        p;
    std::string err;
    if (!p.start(argv, workDir, &err, pathPrepend)) {
        if (out) *out = err;
        return false;
    }
    std::string acc;
    auto        t0 = std::chrono::steady_clock::now();
    while (p.running()) {
        p.pump(&acc);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();
        if (timeoutMs > 0 && ms > timeoutMs) {
            p.kill();
            acc += "\n[Easel] 超时了（" + std::to_string(timeoutMs) + " 毫秒），已经把它停掉。\n";
            if (out) *out = acc;
            if (exitCode) *exitCode = -2;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    p.pump(&acc);
    if (out) *out = acc;
    if (exitCode) *exitCode = p.exitCode();
    return true;
}

}  // namespace internal
}  // namespace easel
