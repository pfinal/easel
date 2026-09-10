// Easel — new_project.cpp  工作台的「新建工程」（D-29 / D-30）
//
// 目标：新建出来的工程里，**只有你自己的东西**。
//
//     我的作品/
//     ├── README.md          三句话：改哪两个文件、按钮在哪
//     ├── src/app.cpp        界面
//     ├── src/solver.cpp     算法
//     ├── data/example.json  数据
//     └── assets/map.png     底图
//
// 构建脚本、单头库、构建产物全在 .easel/ 里，你不用知道它存在。
// 「导出源码」时再展开成结构清晰的标准工程（CMakeLists 在根目录、easel_core.h 在 src/）。
#include "internal.h"

#include <cstdio>
#include <cstring>

namespace easel {
namespace internal {

namespace {

// 生成的构建脚本。放在 .easel/ 里，源码在上一级 —— 所以路径都带 ${PROJ}。
// 工程的根目录因此一个 CMake 文件都没有。
const char* const kCMake = R"CMAKE(# 自动生成的构建脚本（新建工程时写出来的）。
# 你不用看它，也不用改。工作台按下「编译并运行」时用的就是这个。
# 想看真正的工程长什么样：工作台 →「导出源码」，那份是标准布局。
cmake_minimum_required(VERSION 3.20)
project({NAME} VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  add_compile_options(/utf-8)
endif()

get_filename_component(PROJ "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)   # 工程根目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
  string(TOUPPER ${cfg} CFG)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

# ---- Easel ----------------------------------------------------------------
# 工程根目录是上一级，不是这里 —— 告诉 Easel，它的「导出工程」「F9 编辑栏」才找得对。
# （只有走下面第 2/3 条「从源码编」的路时这一行才起作用；预编译包里的 libeasel.a
#   早就编好了，那时候 Easel 靠运行时的当前目录认工程 —— 工作台起作品时当前目录
#   就是工程根，所以照样对。）
set(EASEL_PROJECT_DIR "${PROJ}")
include(FetchContent)
if(NOT DEFINED EASEL_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/easel/CMakeLists.txt")
  set(EASEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/easel")
endif()
# 三条路，从快到慢：
#   1. 工具箱里的预编译包 <工具箱>/easel/prebuilt/ —— 几秒钟链上，默认走的就是这条
#   2. Easel 源码树 —— 头一回要连 ImGui/GLFW 一起编，三分钟起步（改 Easel 本身时用）
#   3. 两样都没有，上网拉一份
if(DEFINED EASEL_DIR AND EXISTS "${EASEL_DIR}/prebuilt/lib/cmake/easel/easelConfig.cmake")
  set(easel_DIR "${EASEL_DIR}/prebuilt/lib/cmake/easel")
  find_package(easel CONFIG REQUIRED)
  message(STATUS "用预编译的 Easel: ${easel_DIR}")
elseif(DEFINED EASEL_DIR)
  message(STATUS "用本地的 Easel 源码: ${EASEL_DIR}（第一次要编几分钟）")
  add_subdirectory(${EASEL_DIR} ${CMAKE_BINARY_DIR}/easel-build)
else()
  FetchContent_Declare(easel
    GIT_REPOSITORY https://github.com/pfinal/easel.git
    GIT_TAG        v0.1.0
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(easel)
endif()

# ---- 界面（你的作品）-----------------------------------------------------
add_executable(app "${PROJ}/src/app.cpp")
target_link_libraries(app PRIVATE easel::easel)
target_include_directories(app PRIVATE "${PROJ}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
if(WIN32)
  set_target_properties(app PROPERTIES WIN32_EXECUTABLE FALSE)
endif()
foreach(dir data assets)
  if(EXISTS "${PROJ}/${dir}")
    add_custom_command(TARGET app POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJ}/${dir}" $<TARGET_FILE_DIR:app>/${dir})
  endif()
endforeach()

# ---- 算法的命令行版（D-23：同一份 solver.cpp，三个入口）--------------------
add_executable(solver "${PROJ}/src/solver.cpp")
target_compile_definitions(solver PRIVATE EASEL_STANDALONE)
target_include_directories(solver PRIVATE "${PROJ}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
if(WIN32)
  # cli::parse() 用 CommandLineToArgvW（shellapi.h）把命令行转成 UTF-8；solver 裸编 easel_core.h，
  # 不经过 easel::easel，这里要单独链一次 shell32。
  target_link_libraries(solver PRIVATE shell32)
endif()

# ---- 测试（有 tests/ 才建）-------------------------------------------------
# doctest::doctest 三条路都有：预编译包的 easelConfig 里 find_dependency(doctest) 建好了，
# 源码路和 FetchContent 路由 Easel 自己的 CMakeLists 建。所以这里不用再找一次。
if(EXISTS "${PROJ}/tests/test_solver.cpp")
  enable_testing()
  add_executable(tests "${PROJ}/tests/test_solver.cpp")
  target_link_libraries(tests PRIVATE doctest::doctest)
  target_include_directories(tests PRIVATE "${PROJ}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME solver_tests COMMAND tests)
endif()
)CMAKE";

const char* const kReadme = R"MD(# {NAME}

用 [Easel](https://github.com/pfinal/easel) 做的算法可视化作品。

## 你要改的就两个文件

| 文件 | 写什么 |
|---|---|
| `src/solver.cpp` | **逻辑**：数据、状态、每帧要算的东西（做算法类作品时它还能单独在终端里跑） |
| `src/app.cpp` | **界面**：画什么（`onDraw`）、右边面板上有哪些控件（`onPanel`） |

{DATA}

## 怎么跑

回到**工作台**，点「编译并运行」。作品会在另一个窗口里打开。
编译错了，输出区里红色那行点一下，就跳到出错的地方。

写完了：工作台 →「生成 exe」出可以双击的程序，「导出源码」出可独立编译的完整工程。

## 想加测试

新建一个 `tests/test_solver.cpp`，第一行 `#include "../src/solver.cpp"`，
然后用 [doctest](https://github.com/doctest/doctest) 写用例。工作台下次编译时会自动带上它。

## 出问题了

- 程序里按 **F12** 打开调试台：日志 / 追踪 / 状态 / 画布 / 用例 / 自检六页
- 状态栏左边那个点：绿 = 正常，黄 = 有警告，红 = 断言失败或崩过
- 界面打不开或者画面不对：工作台的输出区里有全部信息，把这段完整复制下来求助
- 完整的 API 参考：Easel 仓库的 `docs/reference.md`
)MD";

const char* const kSettings = R"JSON({
  "files.encoding": "utf8",
  "C_Cpp.default.cppStandard": "c++17",
  "C_Cpp.default.includePath": ["${workspaceFolder}/src", "${workspaceFolder}/.easel"],
  "C_Cpp.default.defines": ["EASEL_STANDALONE"],
  "cmake.configureOnOpen": false,
  "search.exclude": { "**/.easel": true },
  "// 下面这段把构建脚本和库折起来": "想看它们，把 files.exclude 整段删掉",
  "files.exclude": { ".easel": true }
}
)JSON";

std::string fill(const char* tpl, const std::string& name) {
    std::string s = tpl, key = "{NAME}";
    for (size_t at = s.find(key); at != std::string::npos; at = s.find(key))
        s.replace(at, key.size(), name);
    return s;
}

bool asciiOnly(const std::string& s) {
    for (unsigned char c : s)
        if (c > 127) return false;
    return true;
}

// 从模板拷一个文件过来（模板里没有就跳过，不算失败）
bool take(const std::string& tpl, const std::string& dir, const char* rel, int* files) {
    std::string from = joinPath(tpl, rel);
    if (!existsU8(from)) return false;
    if (!copyTreeU8(from, joinPath(dir, rel), files)) return false;
    return true;
}

}  // namespace

NewProjectReport createProject(const NewProjectOptions& opt) {
    NewProjectReport r;
    std::string      name = opt.name;
    while (!name.empty() && (name.back() == ' ' || name.back() == '/' || name.back() == '\\'))
        name.pop_back();
    if (name.empty()) {
        r.error = "作品名不能是空的";
        return r;
    }
    if (name.find_first_of("/\\:*?\"<>|") != std::string::npos) {
        r.error = "作品名里不能有 / \\ : * ? \" < > | 这些字符";
        return r;
    }
    if (opt.parentDir.empty()) {
        r.error = "选一个存在的目录来放这个工程";
        return r;
    }
    if (!isDirU8(opt.parentDir) && !makeDirsU8(opt.parentDir)) {
        r.error = "建不出目录：" + opt.parentDir;
        return r;
    }

    // 空工程用 template-hello/（两个文件），完整骨架用 template/
    std::string tpl = opt.templateDir;
    if (tpl.empty())
        tpl = joinPath(opt.easelDir, opt.fullSkeleton ? "template" : "template-hello");
    if (!existsU8(joinPath(tpl, "src/solver.cpp"))) {
        r.error = "找不到工程模板：" + tpl;
        return r;
    }

    std::string dir = joinPath(opt.parentDir, name);
    if (existsU8(dir) && !listDirU8(dir).empty()) {
        r.error = dir + " 已经有东西了。换个名字，或者直接「打开工程」。";
        return r;
    }
    r.dir = dir;
    if (!makeDirsU8(dir)) {
        r.error = "建不出目录：" + dir;
        return r;
    }

    // ---- 你的东西：两个源文件 + 数据 + 底图 ----
    if (!take(tpl, dir, "src/app.cpp", &r.files) || !take(tpl, dir, "src/solver.cpp", &r.files)) {
        r.error = "模板里缺 src/app.cpp 或 src/solver.cpp：" + tpl;
        return r;
    }
    take(tpl, dir, "data", &r.files);      // 空工程没有这两样，take() 会自己跳过
    take(tpl, dir, "assets", &r.files);
    if (opt.withTests) take(tpl, dir, "tests", &r.files);

    // ---- 脚手架：全进 .easel/，你看不见 ----
    std::string hidden = joinPath(dir, ".easel");
    // 单头库统一从完整模板那儿取（amalgamate.py 只同步那一份）
    std::string core = joinPath(tpl, "src/easel_core.h");
    if (!existsU8(core)) core = joinPath(joinPath(opt.easelDir, "template"), "src/easel_core.h");
    if (!copyFileU8(core, joinPath(hidden, "easel_core.h"))) {
        r.error = "找不到 easel_core.h（在 Easel 目录跑一次 python3 scripts/amalgamate.py）";
        return r;
    }
    ++r.files;
    writeTextU8(joinPath(hidden, "CMakeLists.txt"),
                fill(kCMake, asciiOnly(name) ? name : "my_project"));
    {
        std::string readme = fill(kReadme, name);
        std::string key = "{DATA}";
        std::string data =
            opt.fullSkeleton
                ? "`data/example.json` 是输入数据，换成你自己的。\n"
                  "想铺一张底图（地图、平面图、照片），把图片放进 `assets/` —— "
                  "`src/app.cpp` 里有加载它的那一行。"
                : "现在这份是**空工程**：一个 Hello 而已。要读数据文件、逐帧回放、画收敛曲线，"
                  "新建工程时勾上「带回放的完整骨架」，或者去看 Easel 的 `examples/sort`。";
        size_t at = readme.find(key);
        if (at != std::string::npos) readme.replace(at, key.size(), data);
        writeTextU8(joinPath(dir, "README.md"), readme);
    }
    writeTextU8(joinPath(dir, ".vscode/settings.json"), kSettings);
    r.files += 3;

    // ---- 作品名写进标题 ----
    std::string app;
    if (readTextU8(joinPath(dir, "src/app.cpp"), &app)) {
        size_t at = app.find("app.title(\"我的作品\")");
        if (at != std::string::npos) {
            app.replace(at, std::strlen("app.title(\"我的作品\")"), "app.title(\"" + name + "\")");
            writeTextU8(joinPath(dir, "src/app.cpp"), app);
        }
    }

    EASEL_LOG("新建工程：%s（%d 个文件）", dir.c_str(), r.files);
    r.ok = true;
    return r;
}

}  // namespace internal
}  // namespace easel
