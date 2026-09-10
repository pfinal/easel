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

void startConfigure() {
    WB&                      w = wb();
    std::vector<std::string> argv = configureArgs(w.projectDir, buildDir(w.projectDir),
                                                  "RelWithDebInfo");
    say("$ " + Proc::describe(argv));
    say("第一次要把界面库编出来，几分钟。以后就只编你改的那几行了。");
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
    say("$ " + Proc::describe(argv));
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
        addOut("没找到编出来的程序：" + argv[0], 1);
        w.stage = Idle;
        return;
    }
    say("▶ 运行 " + baseName(argv[0]) + "（作品的窗口会自己弹出来）");
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
    w.out.clear();
    w.pending.clear();
    w.errors = w.warnings = 0;
    w.wantPackage = false;
    if (existsU8(joinPath(buildDir(w.projectDir), "CMakeCache.txt")))
        startBuild();
    else
        startConfigure();
}

// 「生成 exe」：Release 编一遍，然后把 exe + assets + data 拷成一个能双击的目录
void finishPackage() {
    WB&         w = wb();
    std::string name = baseName(w.projectDir);
    std::string dest = joinPath(joinPath(w.projectDir, "dist"), name + "-release");
    std::string exe = exeIn(releaseDir(w.projectDir));
    if (!existsU8(exe)) {
        addOut("没找到 Release 版的程序：" + exe, 1);
        return;
    }
    makeDirsU8(dest);
    int files = 0;
    copyFileU8(exe, joinPath(dest, name + (exe.size() > 4 && exe.substr(exe.size() - 4) == ".exe"
                                               ? ".exe"
                                               : "")));
    for (const char* d : {"assets", "data"})
        if (existsU8(joinPath(w.projectDir, d)))
            copyTreeU8(joinPath(w.projectDir, d), joinPath(dest, d), &files);
    writeTextU8(joinPath(dest, "README.txt"),
                name + "\n\n双击 " + name + " 就能运行。\n\n"
                "Windows 上如果弹出蓝色的「Windows 已保护你的电脑」：\n"
                "  点「更多信息」→「仍要运行」。这是没买代码签名证书的新程序的默认提示。\n\n"
                "macOS 上如果提示「无法验证开发者」：右键点它 →「打开」→ 再点一次「打开」。\n\n"
                "程序打不开或者画面不对，命令行里跑一下：" + name + " --doctor\n");
    say("生成好了：" + dest);
    if (App::instance()) App::instance()->toast("exe 生成好了");
}

void startPackage() {
    WB&              w = wb();
    if (w.projectDir.empty() || w.proc.running()) return;
    w.out.clear();
    w.errors = w.warnings = 0;
    w.wantPackage = true;
    std::string              rel = releaseDir(w.projectDir);
    std::vector<std::string> argv = configureArgs(w.projectDir, rel, "Release");
    say("$ " + Proc::describe(argv));
    say("生成交付用的 exe：优化过、不带调试信息，比平时慢一点编。");
    w.stage = Configuring;
    if (!spawn(argv, w.projectDir)) return;
    w.status = "配置 Release…";
}

void pump() {
    WB& w = wb();
    if (w.stage == Idle && !w.proc.running()) return;

    std::string chunk;
    bool        alive = w.proc.pump(&chunk);
    if (!chunk.empty()) feed(chunk);
    if (alive) return;
    if (!w.pending.empty()) {
        feed("\n");
        w.pending.clear();
    }

    int    code = w.proc.exitCode();
    double secs = ImGui::GetTime() - w.startedAt;
    char   buf[200];

    if (w.stage == Configuring) {
        if (code != 0) {
            std::snprintf(buf, sizeof buf, "配置失败（退出码 %d）。上面几行是原因。", code);
            addOut(buf, 1);
            w.status = "配置失败";
            w.stage = Idle;
            return;
        }
        if (w.wantPackage) {
            const Toolchain& tc = toolchain();
            std::string              rel = releaseDir(w.projectDir);
            std::vector<std::string> argv{tc.cmake.empty() ? "cmake" : tc.cmake, "--build", rel,
                                          "--config", "Release", "--target", "app", "--parallel"};
            say("$ " + Proc::describe(argv));
            w.stage = Building;
            spawn(argv, w.projectDir);
            w.status = "编译 Release…";
        } else {
            startBuild();
        }
        return;
    }

    if (w.stage == Building) {
        if (code != 0) {
            std::snprintf(buf, sizeof buf, "编译失败：%d 个错误、%d 个警告（%.1f 秒）。点红色那行跳到代码里。",
                          w.errors, w.warnings, secs);
            addOut(buf, 1);
            w.status = "编译失败";
            w.stage = Idle;
            return;
        }
        std::snprintf(buf, sizeof buf, "编译成功（%.1f 秒%s）", secs,
                      w.warnings ? "，有警告" : "");
        say(buf);
        if (w.wantPackage) {
            w.wantPackage = false;
            w.stage = Idle;
            w.status = "就绪";
            finishPackage();
        } else {
            startApp();
        }
        return;
    }

    if (w.stage == Running) {
        if (code == 0) {
            std::snprintf(buf, sizeof buf, "作品正常退出（跑了 %.0f 秒）", secs);
            say(buf);
        } else if (code == -2) {
            say("已停止");
        } else if (code >= 128) {
            std::snprintf(buf, sizeof buf,
                          "作品被信号 %d 打断 —— 多半是越界或空指针。改完再跑一次；"
                          "要查具体哪一行，用 VS Code 的 F5 调试。",
                          code - 128);
            addOut(buf, 1);
        } else {
            std::snprintf(buf, sizeof buf, "作品退出，退出码 %d", code);
            addOut(buf, 2);
        }
        w.stage = Idle;
        w.status = "就绪";
    }
}

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
        say("正在导出完整源码…（几十 MB，界面会卡一下）");
        ExportReport rep = exportProject(o);
        for (const std::string& l : rep.checks)
            addOut(l, l.rfind("[×]", 0) == 0 ? 1 : l.rfind("[!]", 0) == 0 ? 2 : 0);
        if (rep.ok) say("导出好了：" + rep.outDir);
        else addOut("导出失败：" + rep.error, 1);
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
    App app(argc, argv);
    app.title("Easel").size(1100, 720).theme(Theme::Forest()).editorEnabled(false)
       .debugConsoleEnabled(false)    // 工具类程序不需要调试台
       .idleThrottle(true)            // 界面静止的时候（没人点、也没有子进程在编译/跑）降到 10 帧省电
       .busyWhen([]{ return wb().proc.running(); });   // 编译/运行中也保持满帧
    app.statusBar(true);

    WB& w = wb();
#if defined(EASEL_SOURCE_DIR)
    if (existsU8(EASEL_SOURCE_DIR)) w.easelDir = EASEL_SOURCE_DIR;
#endif
    if (w.easelDir.empty() || !existsU8(joinPath(w.easelDir, "template"))) {
        // 工具箱布局：<工具箱>/easel/
        const Toolchain& tc = toolchain();
        if (!tc.kit.empty() && existsU8(joinPath(tc.kit, "easel/template")))
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

    if (!cli::args().at(0).empty()) useProject(absPath(cli::args().at(0)));

    // --run：开起来就编一次跑一次（CI 和「截个图看看」用）
    bool runOnce = cli::args().has("run");
    app.onFrame([&runOnce](double) {
        pump();
        if (runOnce && shared().frame > 2) {
            runOnce = false;
            compileAndRun();
        }
    });
    app.onWindow([&app](const Rect& r) { draw(r, app.theme(), (float)app.dpiScale()); });
    app.onKey([](int key) {
        if (key == ImGuiKey_F5) compileAndRun();
    });
    app.status("Easel");
    return app.run();
}
