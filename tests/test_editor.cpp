// 编辑栏与「导出工程」的测试（D-28）。
// 这一份链接的是完整的 easel 库，但不开窗口 —— CI 上无头也能跑。
#include <doctest/doctest.h>

#include "../src/internal.h"

#include <cstdlib>

using namespace easel;
using namespace easel::internal;

namespace {
// 系统临时目录：跟仓库检出到哪个路径无关（D-36 的 ASCII 父目录校验需要一个保证是
// ASCII 的地方建测试工程；fs::cwd() 在某些机器上可能落在带中文的路径下）。
std::string systemTempDir() {
#if defined(_WIN32)
    const char* t = std::getenv("TEMP");
    if (!t) t = std::getenv("TMP");
    return t ? t : "C:\\Windows\\Temp";
#else
    const char* t = std::getenv("TMPDIR");
    return t && *t ? t : "/tmp";
#endif
}
}  // namespace

TEST_CASE("诊断行解析：能认出文件、行、列和严重程度") {
    std::string f;
    int         line = 0, col = 0, kind = 0;

    CHECK(parseDiagnostic("src/solver.cpp:12:5: error: 'x' was not declared", &f, &line, &col, &kind));
    CHECK(f == "src/solver.cpp");
    CHECK(line == 12);
    CHECK(col == 5);
    CHECK(kind == 1);

    CHECK(parseDiagnostic("src/solver.cpp:87: warning: unused variable", &f, &line, &col, &kind));
    CHECK(line == 87);
    CHECK(col == 0);
    CHECK(kind == 2);

    // Windows 的盘符不能被当成文件名的一部分吃掉
    CHECK(parseDiagnostic("C:/kit/src/solver.cpp:3:1: error: 炸了", &f, &line, &col, &kind));
    CHECK(f == "C:/kit/src/solver.cpp");
    CHECK(line == 3);

    // 中文 locale 的 gcc
    CHECK(parseDiagnostic("src/solver.cpp:9:2: 错误：未声明的标识符", &f, &line, &col, &kind));
    CHECK(line == 9);
    CHECK(kind == 1);

    // 认不出来的就老实说认不出来，别瞎猜
    CHECK_FALSE(parseDiagnostic("最终目标值 2079.59", &f, &line, &col, &kind));
    CHECK_FALSE(parseDiagnostic("In file included from src/app.cpp:3:", &f, &line, &col, &kind));
    CHECK_FALSE(parseDiagnostic("", &f, &line, &col, &kind));
}

TEST_CASE("子进程：把输出捞回来，退出码是对的") {
#if defined(_WIN32)
    std::vector<std::string> argv{"cmd.exe", "/c", "echo hello"};
#else
    std::vector<std::string> argv{"/bin/echo", "hello"};
#endif
    std::string out;
    int         code = -1;
    REQUIRE(runBlocking(argv, {}, 10000, &out, &code));
    CHECK(out.find("hello") != std::string::npos);
    CHECK(code == 0);
}

TEST_CASE("子进程：pump 是非阻塞的 —— 进程还在跑的时候立刻就返回") {
    Proc        p;
    std::string err;
#if defined(_WIN32)
    bool started = p.start({"cmd.exe", "/c", "ping -n 3 127.0.0.1 > nul"}, {}, &err);
#else
    bool started = p.start({"/bin/sleep", "1"}, {}, &err);
#endif
    REQUIRE_MESSAGE(started, err);
    Stopwatch sw;
    std::string out;
    for (int i = 0; i < 50; ++i) p.pump(&out);   // 50 次 pump 也不该等上一秒
    CHECK(sw.ms() < 200.0);
    CHECK(p.running());
    p.kill();
    CHECK_FALSE(p.running());
}

TEST_CASE("子进程：起不来的时候要说清楚，而不是假装在跑") {
    Proc        p;
    std::string err;
    CHECK_FALSE(p.start({"这个程序肯定不存在_easel"}, {}, &err));
    CHECK_FALSE(err.empty());
    CHECK_FALSE(p.running());
}

TEST_CASE("找编译器：找到了就该是个真文件，没找到就得留下一句话") {
    const Toolchain& tc = toolchain(true);
    if (tc.cxx.empty()) {
        CHECK_FALSE(tc.note.empty());
    } else {
        CHECK(existsU8(tc.cxx));
        CHECK_FALSE(tc.source.empty());
    }
}

TEST_CASE("looksLikeProject：只有 src/ 目录不算数，甚至只有 app.cpp 也不算数") {
    // 回归测试：resolvePaths() 曾经只看 <候选目录>/src 存不存在，导致从 easel 仓库
    // 根目录（自己的 src/ 下也有个 app.cpp，库内部实现，凑巧同名）起进程时，
    // projectDir 被误判成 easel 仓库自己，「导出工程」把整个 easel 仓库当学生工程导出，
    // 自检里「有 src/solver.cpp」变成 [×]。
    std::string tmp = joinPath(fs::cwd(), "test_lookslikeproject_tmp");
    removeTreeU8(tmp);

    // 长得像 easel 仓库自己：src/ 存在，但是空的——没有 app.cpp，也没有 solver.cpp
    std::string easelLike = joinPath(tmp, "easel-like");
    makeDirsU8(joinPath(easelLike, "src"));
    CHECK_FALSE(looksLikeProject(easelLike));

    // 更贴近真实场景：src/ 下有 app.cpp，但没有 solver.cpp（这正是 easel 仓库自己的样子）
    std::string appOnly = joinPath(tmp, "app-only");
    makeDirsU8(joinPath(appOnly, "src"));
    writeTextU8(joinPath(appOnly, "src/app.cpp"), "// 不是学生工程");
    CHECK_FALSE(looksLikeProject(appOnly));

    // 真正的学生工程：src/app.cpp 和 src/solver.cpp 都在
    std::string proj = joinPath(tmp, "proj");
    makeDirsU8(joinPath(proj, "src"));
    writeTextU8(joinPath(proj, "src/app.cpp"), "// 学生的 app.cpp");
    writeTextU8(joinPath(proj, "src/solver.cpp"), "// 学生的算法");
    CHECK(looksLikeProject(proj));

    removeTreeU8(tmp);
}

TEST_CASE("导出工程：目录结构、自检文件、不覆盖别人的目录") {
    std::string tmp = joinPath(fs::cwd(), "test_export_tmp");
    removeTreeU8(tmp);   // 上一次跑剩下的先清掉，ctest 连跑多次才不会撞上「已经导过一次」
    std::string out = joinPath(tmp, "Work");
    const EditorPaths& paths = editorPaths();   // 测试跑在 easel 仓库里，template/ 就在旁边

    ExportOptions opt;
    // 显式指定要导出的工程，不依赖 editorPaths() 的推断——ctest 是从 build/ 里跑的，
    // 既不是学生工程根，也不该指望 looksLikeProject() 兜底把 easel 仓库自己当工程
    // （它现在确实不会了，这正是 D-28 那次回归要修的）。
    opt.projectDir = joinPath(paths.easelDir.empty() ? fs::cwd() : paths.easelDir, "template");
    opt.outDir = out;
    opt.name = "Work";
    opt.withEasel = false;   // 测试里不拷 vendor/，太重
    opt.verify = false;
    ExportReport r = exportProject(opt);

    CHECK(r.error.empty());
    CHECK(existsU8(joinPath(out, "CHECK.txt")));
    CHECK(existsU8(joinPath(out, "BUILD.txt")));
    CHECK(existsU8(joinPath(out, "CMakeLists.txt")));
    CHECK_FALSE(existsU8(joinPath(out, "build")));   // 构建产物不该进交付物
    CHECK(r.files > 0);

    // 同一个目录再导一次：认得出是自己上次导的，可以覆盖
    ExportReport again = exportProject(opt);
    CHECK(again.error.empty());

    // 别人的目录：一律不碰
    std::string foreign = joinPath(tmp, "foreign");
    makeDirsU8(foreign);
    writeTextU8(joinPath(foreign, "important.txt"), "别删我");
    ExportOptions o2 = opt;
    o2.outDir = foreign;
    ExportReport r2 = exportProject(o2);
    CHECK_FALSE(r2.ok);
    CHECK_FALSE(r2.error.empty());
    CHECK(existsU8(joinPath(foreign, "important.txt")));
}

TEST_CASE("新建工程：作品名进 CMakeLists / app.cpp，构建脚本进 .easel/，单头库不拷贝") {
    // 用系统临时目录而不是 fs::cwd()：D-36 的父目录 ASCII 校验要求这个目录本身干净，
    // 跟仓库检出到哪个路径无关。
    std::string tmp = joinPath(systemTempDir(), "easel_test_newproj_tmp");
    removeTreeU8(tmp);   // 上一次跑剩下的先清掉，ctest 连跑多次才不会撞上「已经建过一次」
    makeDirsU8(tmp);
    const EditorPaths& paths = editorPaths();   // 测试跑在 easel 仓库里，template/ 就在旁边
    std::string easelDir = paths.easelDir.empty() ? fs::cwd() : paths.easelDir;

    NewProjectOptions o;
    o.easelDir = easelDir;
    o.parentDir = tmp;
    o.name = "TestApp";
    NewProjectReport r = createProject(o);   // 默认：空工程（一个文件，D-36）

    REQUIRE_MESSAGE(r.ok, r.error);
    // 学生看到的就这一个文件，别的一律不许出现在根目录
    CHECK(existsU8(joinPath(r.dir, "src/app.cpp")));
    CHECK_FALSE(existsU8(joinPath(r.dir, "src/solver.cpp")));   // 空工程没有算法文件
    CHECK_FALSE(existsU8(joinPath(r.dir, "README.md")));        // 不再生成 README（D-36）
    for (const char* junk : {"CMakeLists.txt", "CMakePresets.json", "跑.sh", "跑.bat", "run.bat",
                             "docs", "build", "tests", "src/easel_core.h", "data", "assets"})
        CHECK_MESSAGE(!existsU8(joinPath(r.dir, junk)), junk);

    // ls 根目录（不算隐藏项）只有 src
    {
        std::vector<DirEntry> top = listDirU8(r.dir);
        std::vector<std::string> visible;
        for (const DirEntry& e : top)
            if (!e.name.empty() && e.name[0] != '.') visible.push_back(e.name);
        REQUIRE(visible.size() == 1);
        CHECK(visible[0] == "src");
    }

    // 构建脚本全在 .easel/ 里，但单头库不拷贝进去了（D-36：直接指到 <easelDir>/dist）
    CHECK(existsU8(joinPath(r.dir, ".easel/CMakeLists.txt")));
    CHECK_FALSE(existsU8(joinPath(r.dir, ".easel/easel_core.h")));

    std::string cmake, appcpp, settings;
    REQUIRE(readTextU8(joinPath(r.dir, ".easel/CMakeLists.txt"), &cmake));
    REQUIRE(readTextU8(joinPath(r.dir, "src/app.cpp"), &appcpp));
    REQUIRE(readTextU8(joinPath(r.dir, ".vscode/settings.json"), &settings));
    CHECK(cmake.find("${PROJ}/src/app.cpp") != std::string::npos);
    CHECK(cmake.find("EASEL_CORE_DIR") != std::string::npos);
    CHECK(appcpp.find("app.title(\"TestApp\")") != std::string::npos);
    CHECK(settings.find("\"files.exclude\"") != std::string::npos);
    CHECK(settings.find("/dist") != std::string::npos);     // 单头库 include 路径
    CHECK(settings.find("/include") != std::string::npos);  // 分头文件 include 路径
    // 预编译包和源码树的版本核对（B-4）：这两个戳都得被生成的构建脚本用到，
    // 不然预编译包和源码对不上号的问题会被悄悄放过
    CHECK(cmake.find("easel-prebuilt.json") != std::string::npos);
    CHECK(cmake.find("VERSION.json") != std::string::npos);

    // 同一个名字再建一次：拒绝，且不动已有的东西
    NewProjectReport again = createProject(o);
    CHECK_FALSE(again.ok);
    CHECK_FALSE(again.error.empty());

    // 名字里有斜杠之类的，直接挡掉
    NewProjectOptions bad = o;
    bad.name = "a/b";
    CHECK_FALSE(createProject(bad).ok);

    // 中文作品名直接拒绝，并且说清楚原因（D-36：编译器对中文路径支持不好）
    NewProjectOptions badName = o;
    badName.name = "中文";
    NewProjectReport badNameReport = createProject(badName);
    CHECK_FALSE(badNameReport.ok);
    CHECK(badNameReport.error.find("英文字母") != std::string::npos);

    // 父目录含中文也拒绝
    NewProjectOptions badParent = o;
    badParent.name = "AnotherApp";
    badParent.parentDir = joinPath(tmp, "中文目录");
    NewProjectReport badParentReport = createProject(badParent);
    CHECK_FALSE(badParentReport.ok);
    CHECK(badParentReport.error.find("中文") != std::string::npos);

    // 勾了「算法骨架」才有数据文件和示例数据
    NewProjectOptions full = o;
    full.name = "Full";
    full.fullSkeleton = true;
    NewProjectReport fr = createProject(full);
    REQUIRE_MESSAGE(fr.ok, fr.error);
    CHECK(existsU8(joinPath(fr.dir, "data/example.json")));
    CHECK(existsU8(joinPath(fr.dir, "src/solver.cpp")));
    CHECK_FALSE(existsU8(joinPath(fr.dir, ".easel/easel_core.h")));
    CHECK_FALSE(existsU8(joinPath(fr.dir, "CMakeLists.txt")));

    // parentDir 指向一个还不存在的多级目录：自动建出来（CI 里 --new /tmp/blank 这种）
    std::string deepParent = joinPath(tmp, "a/b/c");
    CHECK_FALSE(existsU8(deepParent));
    NewProjectOptions deep = o;
    deep.parentDir = deepParent;
    deep.name = "Deep";
    NewProjectReport dr = createProject(deep);
    REQUIRE_MESSAGE(dr.ok, dr.error);
    CHECK(existsU8(joinPath(deepParent, "Deep/src/app.cpp")));

    // --example hello：从 examples/hello/main.cpp 建，src/app.cpp 里含 Hello
    NewProjectOptions ex = o;
    ex.name = "Ring";
    ex.exampleDir = joinPath(joinPath(easelDir, "examples"), "hello");
    NewProjectReport er = createProject(ex);
    REQUIRE_MESSAGE(er.ok, er.error);
    std::string exApp;
    REQUIRE(readTextU8(joinPath(er.dir, "src/app.cpp"), &exApp));
    CHECK(exApp.find("Hello") != std::string::npos);
}
