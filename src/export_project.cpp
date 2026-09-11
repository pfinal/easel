// Easel — export_project.cpp  「导出工程」：吐出一个离了 Easel 也活得下去的完整工程
//
// 为什么要在 C++ 里再写一遍（scripts/package.py 已经有一份）：
// 绿色工具箱里只有 gcc / cmake / ninja，没有 Python。打最终发布包
// 用 package.py，平时导出用这个按钮 —— 两边产出的目录结构是同一个。
//
// 「完整」的定义（D-28）：把导出的目录拷到一台没装过任何东西、也不联网的机器上，
//   1. 一条 g++ 能编 src/solver.cpp（因为 src/easel.hpp 就在旁边，零依赖）；
//   2. cmake + 编译器能编出整个界面程序（因为 easel/ 和它的 vendor/ 都在包里，
//      顶层 CMakeLists 会自己发现 easel/，不用加任何参数、不用 FetchContent 联网）。
#include "internal.h"

#include <cstdio>
#include <cstring>

namespace easel {
namespace internal {

namespace {

// 这些不进导出包：构建产物、版本库、编辑器的垃圾、自己的历史导出
bool skipName(const std::string& name, bool isDir) {
    static const char* const kDirs[] = {"build",   "dist",  ".git",  ".cache", "__pycache__",
                                        ".idea",   ".vs",   "vendor-cache",
                                        "node_modules", nullptr};
    static const char* const kFiles[] = {".DS_Store", "imgui.ini", ".gitkeep", "a.out", "solver",
                                         "solver.exe", "app", "app.exe", nullptr};
    if (isDir) {
        for (const char* const* p = kDirs; *p; ++p)
            if (name == *p) return true;
        if (name.rfind("build", 0) == 0) return true;   // build_test_tmp 之类
        return false;
    }
    for (const char* const* p = kFiles; *p; ++p)
        if (name == *p) return true;
    auto ends = [&](const char* s) {
        size_t n = std::strlen(s);
        return name.size() > n && name.compare(name.size() - n, n, s) == 0;
    };
    return ends(".o") || ends(".obj") || ends(".ilk") || ends(".pdb") || ends(".zip");
}

}  // namespace

bool copyTreeU8(const std::string& from, const std::string& to, int* files, long long* bytes) {
    if (!isDirU8(from)) {
        if (!copyFileU8(from, to)) return false;
        long long m = 0, sz = 0;
        fileStamp(to, &m, &sz);
        if (files) ++*files;
        if (bytes) *bytes += sz;
        return true;
    }
    if (!makeDirsU8(to)) return false;
    bool ok = true;
    for (const DirEntry& e : listDirU8(from)) {
        if (skipName(e.name, e.isDir)) continue;
        ok = copyTreeU8(joinPath(from, e.name), joinPath(to, e.name), files, bytes) && ok;
    }
    return ok;
}

namespace {

struct Counter {
    int       files = 0;
    long long bytes = 0;
};

bool copyTree(const std::string& from, const std::string& to, Counter* c) {
    return copyTreeU8(from, to, &c->files, &c->bytes);
}

bool copyIfExists(const std::string& from, const std::string& to, Counter* c) {
    if (!existsU8(from)) return false;
    return copyTree(from, to, c);
}

void check(ExportReport* r, bool ok, const std::string& text, bool warnOnly = false) {
    r->checks.push_back((ok ? "[√] " : warnOnly ? "[!] " : "[×] ") + text);
    if (!ok && !warnOnly) r->ok = false;
}

// 导出目录里有没有 src/solver.cpp，是「完整骨架」和「极简工程」唯一的分界线：
// 完整骨架三个入口（solver/tests/app）共用一份 solver.cpp；极简工程（template-hello，
// 现在新建工程的默认布局）只有 src/app.cpp 一个文件，界面和算法都写在里面。
// 后面自检、试编、BUILD.txt 的说明文字全靠这一个判断，别在各处各自
// existsU8(...solver.cpp) 一遍。
bool hasSolverCpp(const std::string& root) { return existsU8(joinPath(root, "src/solver.cpp")); }

// 顶层 CMakeLists 要认得包里自带的 easel/。模板里本来就有这一段（[easel:bundled]），
// 老模板生成的工程没有，就在这里补上 —— 补不上也不算失败，只是要在自检里说清楚。
const char* const kBundleMarker = "[easel:bundled]";
const char* const kBundleBlock =
    "# ---- 包里自带的 Easel  [easel:bundled] ----------------------------------\n"
    "# 导出包里有 easel/ 就直接用它：不联网、不用装、不用加任何 cmake 参数。\n"
    "if(NOT DEFINED EASEL_DIR AND EXISTS \"${CMAKE_CURRENT_SOURCE_DIR}/easel/CMakeLists.txt\")\n"
    "  set(EASEL_DIR \"${CMAKE_CURRENT_SOURCE_DIR}/easel\")\n"
    "  message(STATUS \"用包里自带的 Easel: ${EASEL_DIR}\")\n"
    "endif()\n\n";

bool ensureBundleBlock(const std::string& cmakePath) {
    std::string text;
    if (!readTextU8(cmakePath, &text)) return false;
    if (text.find(kBundleMarker) != std::string::npos) return true;
    size_t at = text.find("if(DEFINED EASEL_DIR)");
    if (at == std::string::npos) return false;
    text.insert(at, kBundleBlock);
    return writeTextU8(cmakePath, text);
}

void collectLicenses(const std::string& easelDir, const std::string& dest, int* n, Counter* c) {
    static const char* const kNames[] = {"LICENSE",     "LICENSE.txt", "LICENSE.md", "LICENSE.TXT",
                                         "LICENSE.MIT", "COPYING",     "LICENSE-MIT", "license.txt",
                                         nullptr};
    if (copyIfExists(joinPath(easelDir, "LICENSE"), joinPath(dest, "Easel-LICENSE.txt"), c)) ++*n;
    std::string vendor = joinPath(easelDir, "vendor");
    if (!isDirU8(vendor)) return;
    for (const DirEntry& d : listDirU8(vendor)) {
        if (!d.isDir) continue;
        for (const char* const* p = kNames; *p; ++p) {
            std::string f = joinPath(joinPath(vendor, d.name), *p);
            if (existsU8(f)) {
                if (copyFileU8(f, joinPath(dest, d.name + "-LICENSE.txt"))) ++*n;
                break;
            }
        }
    }
}

std::string howToBuild(const std::string& name, bool withEasel, bool hiddenCore,
                        bool hasSolver) {
    std::string s;
    s += name + " —— 怎么把它编出来\n";
    s += "================================================\n\n";
    s += "这个目录是自足的：不用装 Easel、不用联网、不用改任何配置文件。\n\n";
    static const char* const kCN[] = {"一", "二", "三", "四"};
    int                      step  = 0;
    if (hasSolver) {
        s += std::string(kCN[step++]) + "、只想跑算法（最快，什么都不用装，只要一个 g++）\n";
        s += hiddenCore ? "    g++ -std=c++17 -DEASEL_STANDALONE -I.easel src/solver.cpp -o solver\n"
                        : "    g++ -std=c++17 -DEASEL_STANDALONE src/solver.cpp -o solver\n";
        s += "    ./solver            （Windows 上是 solver.exe）\n\n";
    } else {
        s += "这是个极简工程（只有 src/app.cpp 一个文件），界面和逻辑都写在里面，\n";
        s += "没有能单独拿 g++ 编的命令行版本，直接看下面「连界面一起编」。\n\n";
    }
    if (withEasel) {
        s += std::string(kCN[step++]) + "、连界面一起编（要 cmake + 编译器；界面用 OpenGL 3.2）\n";
        s += "    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release\n";
        s += "    cmake --build build --config Release --parallel\n";
        s += "    build/bin/app       （Windows 上是 build\\bin\\Release\\app.exe）\n\n";
        s += "    界面库 Easel 的源码在 easel/，它的七个第三方依赖的源码在 easel/vendor/，\n";
        s += "    所以整个过程不联网。cmake 会自己发现它们，不用加参数。\n\n";
        if (hasSolver) {
            s += std::string(kCN[step++]) + "、跑测试\n";
            s += "    cd build && ctest --output-on-failure -C Release\n\n";
            s += "    （-C Release 是为了支持多配置生成器，单配置生成器会自动忽略这个参数）\n\n";
        }
    }
    s += "编不过的时候：把命令行里的报错原样发出去求助，前三行最有用。\n";
    s += "程序跑起来画面不对：app --doctor，把输出发过来（里面有显卡、后端、字体、DPI）。\n";
    return s;
}

}  // namespace

ExportReport exportProject(const ExportOptions& opt) {
    ExportReport r;
    r.ok = true;

    std::string project = opt.projectDir;
    std::string easel = opt.easelDir;
    if (project.empty() || easel.empty()) {   // 作品自己导出自己：路径是编译期常量
        const EditorPaths& paths = editorPaths();
        if (project.empty()) project = paths.projectDir;
        if (easel.empty()) easel = paths.easelDir;
    }
    if (project.empty() || !isDirU8(project)) {
        r.ok = false;
        r.error = "找不到工程目录";
        return r;
    }

    std::string name = opt.name.empty() ? baseName(project) : opt.name;
    if (name.empty()) name = "MySketch";
    std::string root = opt.outDir.empty() ? joinPath(joinPath(project, "dist"), name)
                                          : absPath(opt.outDir);

    // 已经有东西了：只有确认是上一次导出（有 CHECK.txt）才覆盖，
    // 别人的目录一律不碰 —— 手一滑填了个真目录，不能给人删了。
    if (existsU8(root)) {
        if (!existsU8(joinPath(root, "CHECK.txt"))) {
            r.ok = false;
            r.error = root + " 已经存在，而且不像是上一次导出的结果。换个名字或者换个目录。";
            return r;
        }
    }
    if (!makeDirsU8(root)) {
        r.ok = false;
        r.error = "建不出目录：" + root;
        return r;
    }
    r.outDir = root;

    // ---- 1. 工程本体 ----
    // 新式工程（D-30）的脚手架藏在 .easel/ 里；导出时**展开成标准布局** ——
    // 拿到的是一个再普通不过的 CMake 工程：根目录 CMakeLists.txt、src/easel.hpp。
    Counter                  c;
    std::vector<std::string> missing;
    bool folded = existsU8(joinPath(project, ".easel/CMakeLists.txt"));

    if (folded) {
        static const char* const kMine[] = {"src", "tests", "data", "assets", "debug", "README.md",
                                            nullptr};
        for (const char* const* p = kMine; *p; ++p) {
            std::string from = joinPath(project, *p);
            if (!existsU8(from)) {
                missing.push_back(*p);
                continue;
            }
            if (!copyTree(from, joinPath(root, *p), &c)) {
                r.ok = false;
                r.error = "拷不过去：" + from;
                return r;
            }
        }
        // 单头库回到 src/ 旁边：标准工程里它就该在那儿（不再拷进 .easel/，直接从
        // Easel 目录的 dist/ 摊平版取，D-36）
        if (copyFileU8(joinPath(easel, "dist/easel.hpp"), joinPath(root, "src/easel.hpp")))
            ++c.files;
        // 构建脚本用模板那份标准的（它带 [easel:bundled]，认得包里自带的 easel/）
        std::string tpl = joinPath(easel, "template");
        for (const char* f : {"CMakeLists.txt", "CMakePresets.json"})
            if (existsU8(joinPath(tpl, f))) copyTree(joinPath(tpl, f), joinPath(root, f), &c);
        std::string cm;
        if (readTextU8(joinPath(root, "CMakeLists.txt"), &cm)) {
            size_t at = cm.find("project(my_project");
            bool   ascii = true;
            for (unsigned char ch : name)
                if (ch > 127) ascii = false;
            if (at != std::string::npos && ascii)
                cm.replace(at, std::strlen("project(my_project"), "project(" + name);
            writeTextU8(joinPath(root, "CMakeLists.txt"), cm);
        }
    } else {
        // 跑.sh / 跑.bat：模板早改成英文名了（run.sh / run.bat），这两个只是给改名之前就
        // 已经建好的老工程留个后路，万一它们根目录下还留着这两个文件。
        static const char* const kItems[] = {"src",   "tests",   "data",  "assets", "debug", ".easel",
                                             "docs",  ".vscode", ".github", "CMakeLists.txt",
                                             "CMakePresets.json", "README.md", "run.sh", "run.bat",
                                             "跑.sh", "跑.bat", ".gitignore", nullptr};
        for (const char* const* p = kItems; *p; ++p) {
            std::string from = joinPath(project, *p);
            if (!existsU8(from)) {
                missing.push_back(*p);
                continue;
            }
            if (!copyTree(from, joinPath(root, *p), &c)) {
                r.ok = false;
                r.error = "拷不过去：" + from;
                return r;
            }
        }
    }

    // ---- 2. Easel 自己 + 它的依赖源码 ----
    int  licenses = 0;
    bool haveVendor = false;
    if (opt.withEasel) {
        if (easel.empty() || !existsU8(joinPath(easel, "CMakeLists.txt"))) {
            check(&r, false, "找不到 Easel 源码 —— 导出包里没有界面部分，编不出 app", true);
        } else {
            // 只留编出 app 所需要的：cmake（easelConfig 模板）、include/src（库本体）、
            // vendor（依赖源码）、assets（根 CMakeLists 的 POST_BUILD 会拷它，缺了会报错）、
            // LICENSE。docs / scripts / README.md / CMakePresets.json 不进导出包。
            static const char* const kE[] = {"CMakeLists.txt", "cmake", "include", "src",
                                             "vendor", "assets", "LICENSE", nullptr};
            std::string eroot = joinPath(root, "easel");
            for (const char* const* p = kE; *p; ++p)
                copyIfExists(joinPath(easel, *p), joinPath(eroot, *p), &c);
            haveVendor = isDirU8(joinPath(easel, "vendor"));
            collectLicenses(easel, joinPath(root, "licenses"), &licenses, &c);
        }
    }

    // ---- 3. 让顶层 CMakeLists 认得包里的 easel/ ----
    std::string topCMake = joinPath(root, "CMakeLists.txt");
    bool        bundleOk = !opt.withEasel || ensureBundleBlock(topCMake);

    // ---- 4. 说明书 ----
    // 新建工程时库文件被搬进了 .easel/（D-29），裸 g++ 那条命令要带 -I
    bool hiddenCore = existsU8(joinPath(root, ".easel/easel.hpp"));
    // 极简工程（template-hello）没有 src/solver.cpp——一个文件，界面和算法都在
    // src/app.cpp 里。完整骨架（template）三个入口共用一份 solver.cpp。两种布局
    // 后面自检 / 试编 / 说明书都要分别对待，判据只算这一次。
    bool hasSolver = hasSolverCpp(root);
    writeTextU8(joinPath(root, "BUILD.txt"), howToBuild(name, opt.withEasel, hiddenCore, hasSolver));

    // ---- 5. 自检：一条条对着「完整」的定义核 ----
    if (hasSolver) {
        check(&r, true, "有 src/solver.cpp（算法本体）");
    } else {
        check(&r, true, "单文件工程：没有 src/solver.cpp，界面和算法都写在 src/app.cpp 里");
    }
    check(&r, hiddenCore || existsU8(joinPath(root, "src/easel.hpp")),
          std::string("有 easel.hpp（") + (hiddenCore ? ".easel/" : "src/") +
              "）—— 单头零依赖，一条 g++ 就能编算法");
    check(&r, existsU8(topCMake), "有 CMakeLists.txt");
    if (hasSolver) {
        // tests/ 和 data/ 只在完整骨架里才有意义——极简工程压根不带这两个目录，
        // 提示「没有」纯属噪音（这两条本来就是给完整骨架用的提醒）。
        check(&r, existsU8(joinPath(root, "tests")), "有 tests/", true);
        check(&r, existsU8(joinPath(root, "data")), "有 data/", true);
    }
    if (opt.withEasel) {
        check(&r, existsU8(joinPath(root, "easel/CMakeLists.txt")), "包里带着 Easel 源码");
        check(&r, haveVendor,
              haveVendor ? "带着 easel/vendor/（依赖源码齐全，编界面时完全不用联网）"
                         : "没有 easel/vendor/ —— 编界面时要能上 GitHub。"
                           "补救：在 Easel 目录跑 python3 scripts/vendor.py 之后重新导出",
              !haveVendor);
        check(&r, bundleOk,
              bundleOk ? "CMakeLists 会自己发现包里的 easel/（不用加 -DEASEL_DIR）"
                       : "没能往 CMakeLists 里插入自动发现 easel/ 的那几行，"
                         "要自己加 -DEASEL_DIR=./easel",
              !bundleOk);
        check(&r, licenses > 0, "带了 " + std::to_string(licenses) + " 份第三方许可证（MIT 要求）",
              licenses == 0);
    }
    check(&r, !existsU8(joinPath(root, "build")), "包里没有 build/（构建产物不该进交付物）");

    // ---- 6. 真的编一次，证明「独立可编译」不是嘴上说说 ----
    if (opt.verify && !hasSolver) {
        // 极简工程没有 src/solver.cpp 可编：app.cpp 用的是完整 GUI 库，裸 g++ 一条命令
        // 本来就编不出来（要 cmake + easel/ 一起编，见下面 CMakeLists 那几条自检）。
        // 跳过，说明原因——既不装作试编通过，也不当成失败。
        check(&r, false,
              "极简工程没有 src/solver.cpp，跳过了「试编一次」这一步——"
              "app.cpp 用的是完整 GUI 库，裸 g++ 编不出来，是否真的独立可编译要看"
              "下面 cmake 那几条自检",
              true);
    } else if (opt.verify) {
        const Toolchain& tc = toolchain();
        if (tc.cxx.empty()) {
            check(&r, false, "没找到编译器，跳过了「试编一次」这一步（" + tc.note + "）", true);
        } else {
            std::string scratch = joinPath(joinPath(project, "build"), "run");
            makeDirsU8(scratch);
            std::string out = joinPath(scratch, "verify_export");
#if defined(_WIN32)
            out += ".exe";
#endif
            std::string log;
            int         code = -1;
            std::vector<std::string> argv{tc.cxx, "-std=c++17", "-DEASEL_STANDALONE",
                                          "-fdiagnostics-color=never"};
            if (hiddenCore) argv.push_back("-I" + joinPath(root, ".easel"));
            argv.push_back(joinPath(root, "src/solver.cpp"));
            argv.push_back("-o");
            argv.push_back(out);
            bool done = runBlocking(argv, root, 120000, &log, &code);
            bool good = done && code == 0;
            check(&r, good,
                  good ? "试编通过：在导出目录里 g++ 一条命令编出了 solver（这才叫脱得开 Easel）"
                       : "试编没过 —— 导出的 solver.cpp 单独编不出来。头三行报错：\n" +
                             log.substr(0, 400));
        }
    }

    r.files = c.files;
    r.bytes = c.bytes;

    // ---- 7. 写自检文件 ----
    std::string txt;
    txt += "导出自检 —— " + name + "\n";
    txt += "================================================\n\n";
    txt += "导出目录：" + root + "\n";
    char buf[128];
    // 大小一律走 formatBytes()（1 MB = 1024 KB），跟工作台日志里所有「编译完成 →
    // xx MB」「生成完成 → xx MB」是同一个口径。这里以前是 c.bytes / 1e6（十进制 MB），
    // 于是同一次导出会报出两个数：CHECK.txt 和这条日志说 56.0 MB，工作台那行
    // 「导出完成 →（… 53.4 MB）」说 53.4 MB —— 字节数其实一样，只是除数不一样。
    std::snprintf(buf, sizeof buf, "%d 个文件，%s\n\n", c.files, formatBytes(c.bytes).c_str());
    txt += buf;
    for (const std::string& l : r.checks) txt += "  " + l + "\n";
    if (!missing.empty()) {
        txt += "\n工程里本来就没有这些，跳过了：";
        for (size_t i = 0; i < missing.size(); ++i) txt += (i ? "、" : "") + missing[i];
        txt += "\n";
    }
    writeTextU8(joinPath(root, "CHECK.txt"), txt);
    r.checklist = txt;

    EASEL_LOG("导出工程：%s（%d 个文件，%s，%s）", root.c_str(), c.files, formatBytes(c.bytes).c_str(),
              r.ok ? "自检全过" : "自检有问题，看 CHECK.txt");
    return r;
}

}  // namespace internal
}  // namespace easel
