// Easel — new_project.cpp  工作台的「新建工程」（D-29 / D-30 / D-36）
//
// 目标：新建出来的工程里，**只有你自己的东西**。空工程只有一个文件：
//
//     MySketch/
//     └── src/app.cpp        界面（画一个 Hello）
//
// 骨架下拉选「算法骨架」才会多出 src/solver.cpp、data/example.json、assets/map.png。
// 构建脚本、构建产物全在 .easel/ 里，你不用知道它存在；单头库不拷贝，直接指到
// Easel 目录的 dist/easel.hpp。「导出源码」时再展开成结构清晰的标准工程
//（CMakeLists 在根目录、easel.hpp 在 src/）。
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
# 预编译包和源码树是不是同一个 commit：编出来的和源码对不上号是最难查的一类问题
# （easel-prebuilt.json 由 cmake --install 写，VERSION.json 由 make_toolbox.py 打包源码时写）。
# 两个 commit 都读到且不一样才改道；读不到就是没法核对，照旧信预编译包。
set(EASEL_PREBUILT_OK TRUE)
if(DEFINED EASEL_DIR)
  set(_easel_prebuilt_commit "")
  if(EXISTS "${EASEL_DIR}/prebuilt/easel-prebuilt.json")
    file(READ "${EASEL_DIR}/prebuilt/easel-prebuilt.json" _easel_pb_json)
    string(JSON _easel_prebuilt_commit ERROR_VARIABLE _easel_pb_err GET "${_easel_pb_json}" commit)
    if(_easel_pb_err)
      set(_easel_prebuilt_commit "")
    endif()
  endif()

  set(_easel_src_commit "")
  if(EXISTS "${EASEL_DIR}/VERSION.json")
    file(READ "${EASEL_DIR}/VERSION.json" _easel_sv_json)
    string(JSON _easel_src_commit ERROR_VARIABLE _easel_sv_err GET "${_easel_sv_json}" commit)
    if(_easel_sv_err)
      set(_easel_src_commit "")
    endif()
  else()
    # 开发机上 EASEL_DIR 直接指向 Easel 的 git 仓库，没有 VERSION.json 这个文件
    find_program(_easel_git_np git)
    if(_easel_git_np AND EXISTS "${EASEL_DIR}/.git")
      execute_process(
        COMMAND ${_easel_git_np} rev-parse --short=7 HEAD
        WORKING_DIRECTORY "${EASEL_DIR}"
        OUTPUT_VARIABLE _easel_src_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _easel_git_np_rc)
      if(NOT _easel_git_np_rc EQUAL 0)
        set(_easel_src_commit "")
      endif()
    endif()
  endif()

  if(_easel_prebuilt_commit AND _easel_src_commit)
    if(NOT _easel_prebuilt_commit STREQUAL _easel_src_commit)
      message(STATUS "预编译的 Easel 是提交 ${_easel_prebuilt_commit}，源码是提交 ${_easel_src_commit}，"
                      "不一致：改用源码编（几分钟）")
      set(EASEL_PREBUILT_OK FALSE)
    endif()
  else()
    message(STATUS "没法核对版本，照用预编译")
  endif()
endif()

# 三条路，从快到慢：
#   1. 工具箱里的预编译包 <工具箱>/easel/prebuilt/ —— 几秒钟链上，默认走的就是这条
#   2. Easel 源码树 —— 头一回要连 ImGui/GLFW 一起编，三分钟起步（改 Easel 本身时用）
#   3. 两样都没有，上网拉一份
if(DEFINED EASEL_DIR AND EASEL_PREBUILT_OK AND EXISTS "${EASEL_DIR}/prebuilt/lib/cmake/easel/easelConfig.cmake")
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

# solver / tests 直接 #include "easel.hpp"（单头库），不经过 easel::easel 这个 target，
# 所以要单独给一条 include 路径。不拷一份进工程，直接指到 dist/ 下摊平好的那份：
# EASEL_DIR 有定义（预编译包或本地源码两条路）就在 ${EASEL_DIR}/dist；
# FetchContent 远程拉的话在 ${easel_SOURCE_DIR}/dist —— 两边都是 git 仓库里现成的文件。
if(DEFINED EASEL_DIR)
  set(EASEL_CORE_DIR "${EASEL_DIR}/dist")
else()
  set(EASEL_CORE_DIR "${easel_SOURCE_DIR}/dist")
endif()

# ---- 界面（你的作品）-----------------------------------------------------
add_executable(app "${PROJ}/src/app.cpp")
target_link_libraries(app PRIVATE easel::easel)
target_include_directories(app PRIVATE "${PROJ}/src" "${CMAKE_CURRENT_SOURCE_DIR}" "${EASEL_CORE_DIR}")
if(WIN32)
  set_target_properties(app PROPERTIES WIN32_EXECUTABLE TRUE)   # 没有黑框；printf 进 F12 日志窗，命令行下 Easel 会接回父终端
  if(MSVC)
    target_link_options(app PRIVATE "/ENTRY:mainCRTStartup")
  endif()
endif()
foreach(dir data assets)
  if(EXISTS "${PROJ}/${dir}")
    add_custom_command(TARGET app POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJ}/${dir}" $<TARGET_FILE_DIR:app>/${dir})
  endif()
endforeach()

# ---- 算法的命令行版（D-23：同一份 solver.cpp，三个入口；有 src/solver.cpp 才建，D-36）----
if(EXISTS "${PROJ}/src/solver.cpp")
  add_executable(solver "${PROJ}/src/solver.cpp")
  target_compile_definitions(solver PRIVATE EASEL_STANDALONE)
  target_include_directories(solver PRIVATE "${PROJ}/src" "${EASEL_CORE_DIR}")
  if(WIN32)
    # cli::parse() 用 CommandLineToArgvW（shellapi.h）把命令行转成 UTF-8；solver 裸编 easel.hpp，
    # 不经过 easel::easel，这里要单独链一次 shell32。
    target_link_libraries(solver PRIVATE shell32)
  endif()
endif()

# ---- 测试（有 tests/ 才建）-------------------------------------------------
# doctest::doctest 三条路都有：预编译包的 easelConfig 里 find_dependency(doctest) 建好了，
# 源码路和 FetchContent 路由 Easel 自己的 CMakeLists 建。所以这里不用再找一次。
if(EXISTS "${PROJ}/tests/test_solver.cpp")
  enable_testing()
  add_executable(tests "${PROJ}/tests/test_solver.cpp")
  target_link_libraries(tests PRIVATE doctest::doctest)
  target_include_directories(tests PRIVATE "${PROJ}/src" "${EASEL_CORE_DIR}")
  add_test(NAME solver_tests COMMAND tests)
endif()
)CMAKE";

// includePath 里两条 {EASELDIR} 路径是生成时填进去的绝对路径（VS Code 不会跑 cmake
// 去解析 EASEL_DIR，只能给它一份写死的）：dist/ 是单头库摊平的那份（给 solver.cpp 用），
// include/ 是分头文件的那份（给 app.cpp 的 <easel/easel.h> 用）。
const char* const kSettings = R"JSON({
  "files.encoding": "utf8",
  "C_Cpp.default.cppStandard": "c++17",
  "C_Cpp.default.includePath": ["${workspaceFolder}/src", "{EASELDIR}/dist", "{EASELDIR}/include"],
  "C_Cpp.default.defines": ["EASEL_STANDALONE"],
  "cmake.configureOnOpen": false,
  "search.exclude": { "**/.easel": true },
  "// 下面这段把构建脚本折起来": "想看它，把 files.exclude 整段删掉",
  "files.exclude": { ".easel": true }
}
)JSON";

std::string fill(const char* tpl, const std::string& name, const std::string& easelDir = {}) {
    std::string s = tpl;
    auto rep = [&](const std::string& key, const std::string& val) {
        for (size_t at = s.find(key); at != std::string::npos; at = s.find(key))
            s.replace(at, key.size(), val);
    };
    rep("{NAME}", name);
    if (!easelDir.empty()) rep("{EASELDIR}", easelDir);
    return s;
}

bool asciiOnly(const std::string& s) {
    for (unsigned char c : s)
        if (c > 127) return false;
    return true;
}

// 作品名：只许英文字母、数字、下划线、连字符，首字符不能是连字符
// （编译器/CMake 对中文路径支持不好；D-36）。
bool validName(const std::string& s) {
    if (s.empty() || s[0] == '-') return false;
    for (char c : s) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-';
        if (!ok) return false;
    }
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
    if (!validName(name)) {
        r.error = "作品名只能用英文字母、数字、下划线、连字符（编译器对中文路径支持不好）。"
                   "窗口标题可以在 app.cpp 里改成中文。";
        return r;
    }
    if (opt.parentDir.empty()) {
        r.error = "选一个存在的目录来放这个工程";
        return r;
    }
    if (!asciiOnly(opt.parentDir)) {
        r.error = "放工程的目录不能含中文（编译器对中文路径支持不好），换到比如 D:\\projects";
        return r;
    }
    if (!isDirU8(opt.parentDir) && !makeDirsU8(opt.parentDir)) {
        r.error = "建不出目录：" + opt.parentDir;
        return r;
    }

    bool fromExample = !opt.exampleDir.empty();   // exampleDir 非空时和 fullSkeleton 互斥

    // 空工程用 template-hello/（一个文件），算法骨架用 template/；exampleDir 走单独的分支
    std::string tpl = opt.templateDir;
    if (!fromExample) {
        if (tpl.empty())
            tpl = joinPath(opt.easelDir, opt.fullSkeleton ? "template" : "template-hello");
        if (!existsU8(joinPath(tpl, "src/app.cpp"))) {
            r.error = "找不到工程模板：" + tpl;
            return r;
        }
    } else if (!existsU8(joinPath(opt.exampleDir, "main.cpp"))) {
        r.error = "示例目录里没有 main.cpp：" + opt.exampleDir;
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

    // ---- 你的东西：源文件 + 数据 + 底图 ----
    if (fromExample) {
        // 示例：main.cpp 是单文件（界面+算法都在一起），拷成 src/app.cpp
        makeDirsU8(joinPath(dir, "src"));
        if (!copyFileU8(joinPath(opt.exampleDir, "main.cpp"), joinPath(dir, "src/app.cpp"))) {
            r.error = "拷不过去：" + joinPath(opt.exampleDir, "main.cpp");
            return r;
        }
        ++r.files;
        take(opt.exampleDir, dir, "assets", &r.files);
        take(opt.exampleDir, dir, "data", &r.files);
    } else {
        if (!take(tpl, dir, "src/app.cpp", &r.files)) {
            r.error = "模板里缺 src/app.cpp：" + tpl;
            return r;
        }
        take(tpl, dir, "src/solver.cpp", &r.files);   // 空工程没有这个文件，take() 会自己跳过
        if (opt.fullSkeleton) {
            take(tpl, dir, "data", &r.files);
            take(tpl, dir, "assets", &r.files);
        }
        if (opt.withTests) take(tpl, dir, "tests", &r.files);
    }

    // ---- 脚手架：全进 .easel/，你看不见（单头库不拷贝，直接指到 <easelDir>/dist）----
    std::string hidden = joinPath(dir, ".easel");
    if (!opt.easelDir.empty() && !existsU8(joinPath(opt.easelDir, "dist/easel.hpp"))) {
        r.error = "找不到 easel.hpp（在 Easel 目录跑一次 python3 scripts/amalgamate.py）：" +
                   joinPath(opt.easelDir, "dist/easel.hpp");
        return r;
    }
    std::string easelDirFwd = absPath(opt.easelDir);
    for (char& c : easelDirFwd)
        if (c == '\\') c = '/';
    writeTextU8(joinPath(hidden, "CMakeLists.txt"),
                fill(kCMake, asciiOnly(name) ? name : "my_project"));
    writeTextU8(joinPath(dir, ".vscode/settings.json"), fill(kSettings, name, easelDirFwd));
    r.files += 2;

    // ---- 作品名写进标题 ----
    static const char* const kTitlePlaceholders[] = {"MySketch", "我的作品", nullptr};
    std::string app;
    if (readTextU8(joinPath(dir, "src/app.cpp"), &app)) {
        bool changed = false;
        for (const char* const* p = kTitlePlaceholders; *p; ++p) {
            std::string needle = std::string("app.title(\"") + *p + "\")";
            size_t      at = app.find(needle);
            if (at != std::string::npos) {
                app.replace(at, needle.size(), "app.title(\"" + name + "\")");
                changed = true;
                break;
            }
        }
        if (changed) writeTextU8(joinPath(dir, "src/app.cpp"), app);
    }

    EASEL_LOG("新建工程：%s（%d 个文件）", dir.c_str(), r.files);
    r.ok = true;
    return r;
}

}  // namespace internal
}  // namespace easel
