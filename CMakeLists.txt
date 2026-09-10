# ============================================================================
#  Easel — 「画架」算法可视化调试库
#  代码酷 daimaku.net · MIT
#
#  在别的工程里用它：
#      include(FetchContent)
#      FetchContent_Declare(easel
#          GIT_REPOSITORY https://github.com/pfinal/easel.git
#          GIT_TAG        v0.1.0)
#      FetchContent_MakeAvailable(easel)
#      target_link_libraries(myapp PRIVATE easel::easel)
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(easel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(PROJECT_IS_TOP_LEVEL)
  set(EASEL_IS_TOP ON)
else()
  set(EASEL_IS_TOP OFF)
endif()

option(EASEL_BUILD_EXAMPLES "编译 examples/"           ${EASEL_IS_TOP})
option(EASEL_BUILD_TESTS    "编译 tests/"              ${EASEL_IS_TOP})
option(EASEL_ENABLE_ASAN    "打开地址消毒器（查越界）" OFF)
# ---- 预编译包（D-26 绿色工具箱）------------------------------------------
# 打开时会多做两件事：
#   1. 把 glfw / nfd / nlohmann_json / doctest 各自的 install 规则也打开
#      （它们默认只在自己是顶层工程时才生成 install target）；
#   2. 给 easel / easel_imgui 生成 install + export，产出
#      <prefix>/lib/cmake/easel/easelConfig.cmake。
# 学生工程 add_subdirectory 进来时它是 OFF —— 那条路不需要装任何东西。
option(EASEL_INSTALL "生成 install / export 规则（做预编译包用）" ${EASEL_IS_TOP})
# 「导出源码」（D-28）和某些精简拷贝里只带 include/ src/ vendor/，没有 cmake/。
# 那种树不可能拿来做包，别为了一个模板文件把配置直接炸掉。
if(EASEL_INSTALL AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/easelConfig.cmake.in")
  message(STATUS "Easel: 这份源码树里没有 cmake/easelConfig.cmake.in，不生成预编译包")
  set(EASEL_INSTALL OFF)
endif()
set(EASEL_BACKEND "auto" CACHE STRING "渲染后端: auto | gl | dx11")

# ---- 后端选择 --------------------------------------------------------------
# v0.1.0：三平台默认都用 OpenGL（老师在 Mac 上验过）。
# Windows 的 DX11 后端已经写好，但要等 9/21 的 Windows 检查点实测过才会变成默认。
if(EASEL_BACKEND STREQUAL "auto")
  set(EASEL_BACKEND_RESOLVED "gl")
else()
  set(EASEL_BACKEND_RESOLVED "${EASEL_BACKEND}")
endif()
if(EASEL_BACKEND_RESOLVED STREQUAL "dx11" AND NOT WIN32)
  message(FATAL_ERROR "DX11 后端只有 Windows 有")
endif()
message(STATUS "Easel 渲染后端: ${EASEL_BACKEND_RESOLVED}")

# ============================================================================
#  依赖：全部 FetchContent + 静态链接，学生 git clone 之后什么都不用装
# ============================================================================
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---- 离线构建：把依赖源码放进 vendor/，就不再联网 -------------------------
# 机房、还原卡、出不去外网的环境全靠这个。生成 vendor/ 的办法：
#     python3 scripts/vendor.py
# 之后 cmake 会自动发现它，什么参数都不用加。
set(EASEL_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor" CACHE PATH "依赖源码目录（离线用）")
set(_easel_vendored "")
foreach(dep glfw imgui implot nfd nlohmann_json doctest stb imguicolortextedit miniaudio)
  if(EXISTS "${EASEL_VENDOR_DIR}/${dep}")
    string(TOUPPER "${dep}" _DEP)
    set(FETCHCONTENT_SOURCE_DIR_${_DEP} "${EASEL_VENDOR_DIR}/${dep}" CACHE PATH "" FORCE)
    list(APPEND _easel_vendored ${dep})
  endif()
endforeach()
list(LENGTH _easel_vendored _easel_vendored_n)
if(_easel_vendored_n EQUAL 9)
  set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "" FORCE)
  message(STATUS "Easel: 九个依赖都在 ${EASEL_VENDOR_DIR}，本次构建完全离线")
else()
  # 缺哪怕一个都不能离线：上一次是全的、这次加了新依赖时，缓存里的 ON 会把构建卡死
  set(FETCHCONTENT_FULLY_DISCONNECTED OFF CACHE BOOL "" FORCE)
  if(_easel_vendored_n GREATER 0)
    message(STATUS "Easel: vendor/ 里只有 ${_easel_vendored}，其余仍需联网下载")
  endif()
endif()

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# glfw 自带干净的 install/export（装出 lib/cmake/glfw3/ + 无命名空间的 glfw 目标），
# 做预编译包时直接用它，我们的 easelConfig 里再 find_dependency(glfw3) 找回来。
set(GLFW_INSTALL        ${EASEL_INSTALL} CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
  GIT_SHALLOW    TRUE)

FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.92.9b-docking
  GIT_SHALLOW    TRUE)

FetchContent_Declare(implot
  GIT_REPOSITORY https://github.com/epezent/implot.git
  GIT_TAG        v1.0
  GIT_SHALLOW    TRUE)

set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL     ${EASEL_INSTALL} CACHE BOOL "" FORCE)
# nfd v1.3.0 的 install 规则里写的是 ${LIB_INSTALL_DIR}，可它自己从来没给这个变量赋过值。
# 展开成空之后参数表变成 `ARCHIVE DESTINATION PUBLIC_HEADER DESTINATION include`，
# libnfd.a 会被装进 <prefix>/PUBLIC_HEADER/。这里先替它定义好，就不用手写 IMPORTED 目标了。
set(LIB_INSTALL_DIR "lib")
if(EASEL_INSTALL AND APPLE)
  # nfd 在 macOS 上用 find_library(APPKIT_LIBRARY AppKit)，拿到的是当前 SDK 的绝对路径
  # （/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/...），它会被原样写进
  # nfd-config.cmake，包就不可搬迁了（换台 Mac、换个 Xcode 位置就断）。
  # 先把这两个 cache 变量填成链接选项，find_library 就不会再跑，export 里留下的是
  # `-framework AppKit` —— 到哪台 Mac 上都对。
  set(APPKIT_LIBRARY "-framework AppKit" CACHE STRING "" FORCE)
  set(UNIFORMTYPEIDENTIFIERS_LIBRARY "-framework UniformTypeIdentifiers" CACHE STRING "" FORCE)
endif()
FetchContent_Declare(nfd
  GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
  GIT_TAG        v1.3.0
  GIT_SHALLOW    TRUE)

set(JSON_BuildTests OFF CACHE INTERNAL "")
# core.h 要 nlohmann/json.hpp，所以预编译包里必须带上它的头和 config
set(JSON_Install    ${EASEL_INSTALL} CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.12.0
  GIT_SHALLOW    TRUE)

# 学生工程的 tests 目标用 doctest::doctest，所以预编译包里也要装一份（头 + config）
if(EASEL_INSTALL)
  set(DOCTEST_NO_INSTALL OFF CACHE BOOL "" FORCE)
else()
  set(DOCTEST_NO_INSTALL ON  CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG        v2.4.12
  GIT_SHALLOW    TRUE)

FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20)

# 内置编辑栏的文本控件（D-28）。上游没有 tag，钉 commit；只用 TextEditor.{h,cpp}，
# 仓库里的 TextDiff / dtl.h 不编。它只依赖 Dear ImGui 和 C++17 标准库。
FetchContent_Declare(imguicolortextedit
  GIT_REPOSITORY https://github.com/goossens/ImGuiColorTextEdit.git
  GIT_TAG        a20e4493bb618a8b738bbb33e00932f842ae8595)

# 声音（D-33 批次 2）。单头、public domain、三平台后端全内置、自带 wav/mp3/flac 解码。
# 只用仓库根目录的 miniaudio.h，实现放在 src/audio.cpp 里编一次 —— 所以不要 add_subdirectory
# 它自带的 CMakeLists.txt（那会再编一遍同样的实现，还要多开一门 C 语言）。
# SOURCE_SUBDIR 指到一个不存在的子目录，FetchContent 就只下载、不构建。
FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG        0.11.25
  SOURCE_SUBDIR  no-cmake)

FetchContent_MakeAvailable(nlohmann_json doctest stb imguicolortextedit miniaudio)
FetchContent_MakeAvailable(glfw imgui implot nfd)

# ---- Dear ImGui（上游没有 CMakeLists，自己建 target）-----------------------
add_library(easel_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${implot_SOURCE_DIR}/implot.cpp
  ${implot_SOURCE_DIR}/implot_items.cpp
  ${implot_SOURCE_DIR}/implot_demo.cpp
  ${imguicolortextedit_SOURCE_DIR}/TextEditor.cpp)
# 源码构建时指向 FetchContent 的三个源码目录；装出去之后全部摊平在 <prefix>/include/
# （imgui.h、imgui_impl_glfw.h、implot.h 都在同一层，所以只要一个 include 目录）。
# TextEditor.h 是 easel 私有的（D-28 的编辑栏），不装 —— 装出去的 include 里没有它。
target_include_directories(easel_imgui PUBLIC
  $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>
  $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/backends>
  $<BUILD_INTERFACE:${implot_SOURCE_DIR}>
  $<BUILD_INTERFACE:${imguicolortextedit_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>)
target_compile_definitions(easel_imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(easel_imgui PUBLIC glfw)
if(MSVC)
  target_compile_options(easel_imgui PRIVATE /utf-8)
endif()

if(EASEL_BACKEND_RESOLVED STREQUAL "dx11")
  target_sources(easel_imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
  target_link_libraries(easel_imgui PUBLIC d3d11 dxgi d3dcompiler)
else()
  target_sources(easel_imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  find_package(OpenGL REQUIRED)
  target_link_libraries(easel_imgui PUBLIC OpenGL::GL)
endif()

# ============================================================================
#  easel 本体
# ============================================================================
add_library(easel STATIC
  src/app.cpp
  src/camera.cpp
  src/canvas.cpp
  src/ui.cpp
  src/theme.cpp
  src/file.cpp
  src/font.cpp
  src/log_capture.cpp
  src/debug_console.cpp
  src/texture.cpp
  src/backend.cpp
  src/process.cpp
  src/toolchain.cpp
  src/editor.cpp
  src/export_project.cpp
  src/new_project.cpp
  src/audio.cpp)
add_library(easel::easel ALIAS easel)

target_include_directories(easel PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_include_directories(easel PRIVATE ${stb_SOURCE_DIR} ${miniaudio_SOURCE_DIR})
target_link_libraries(easel PUBLIC nlohmann_json::nlohmann_json PRIVATE easel_imgui nfd)
# EASEL_SOURCE_DIR  Easel 自己的源码树（导出时要把它整个带走）
# EASEL_PROJECT_DIR 学生的工程根目录。默认是顶层 CMake 工程；
#                   新式工程（D-30）的构建脚本藏在 .easel/ 里，那时候顶层就不是工程根了，
#                   由它显式 set(EASEL_PROJECT_DIR ...) 指过来。
#
# 注意这两个是 PRIVATE 编进 libeasel.a 的：预编译包（<工具箱>/easel/prebuilt，D-26）
# 里它们指的是**打包那台机器**上的路径，到了学生机上一律不存在。所以运行期
# resolvePaths()（src/editor.cpp）是「当前目录优先、编译期常量垫底」的顺序 ——
# 工作台起作品子进程时把工作目录设成工程根，那条兜底才是预编译路径下的正解。
if(NOT DEFINED EASEL_PROJECT_DIR)
  set(EASEL_PROJECT_DIR "${CMAKE_SOURCE_DIR}")
endif()
target_compile_definitions(easel PRIVATE
  EASEL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  EASEL_PROJECT_DIR="${EASEL_PROJECT_DIR}"
  EASEL_BACKEND_NAME="${EASEL_BACKEND_RESOLVED}"
  $<$<STREQUAL:${EASEL_BACKEND_RESOLVED},dx11>:EASEL_BACKEND_DX11>
  $<$<STREQUAL:${EASEL_BACKEND_RESOLVED},gl>:EASEL_BACKEND_GL>)

# 让用 easel 的工程也能直接写 #include <imgui.h>（D-08 的铁律）
# 装出去之后这三个目录都摊平进 <prefix>/include/，见文件末尾的 install(FILES ...)
target_include_directories(easel PUBLIC
  $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>
  $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/backends>
  $<BUILD_INTERFACE:${implot_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>)
target_compile_definitions(easel PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

if(MSVC)
  target_compile_options(easel PUBLIC /utf-8)
  target_compile_options(easel PRIVATE /W4)
else()
  target_compile_options(easel PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
if(APPLE)
  target_link_libraries(easel PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
  # 声音（miniaudio 的 CoreAudio 后端）。默认它是运行时 dlopen 的，
  # 显式链上更稳，也让 MA_NO_RUNTIME_LINKING 那条路随时能走。
  target_link_libraries(easel PRIVATE "-framework CoreFoundation" "-framework CoreAudio"
                                      "-framework AudioToolbox")
endif()
if(UNIX AND NOT APPLE)
  # Linux/BSD 的 ALSA / PulseAudio 后端是运行时加载的，只要这三个
  find_package(Threads REQUIRED)
  target_link_libraries(easel PRIVATE Threads::Threads m ${CMAKE_DL_LIBS})
endif()
# Windows（WASAPI）不用额外链任何库
if(WIN32)
  target_link_libraries(easel PRIVATE dbghelp)
  target_compile_definitions(easel PUBLIC EASEL_HAVE_DBGHELP)
  # cli::parse() 靠 CommandLineToArgvW（shellapi.h）把命令行转成 UTF-8；STATIC 库的 PRIVATE
  # 依赖会自动传给链接它的可执行文件（workbench/easel_tests），所以这里加一次就够。
  target_link_libraries(easel PRIVATE shell32)
endif()
if(MINGW)
  # 静态链接运行库：交付出去的 exe 不用带 libgcc_s_seh-1.dll / libstdc++-6.dll，
  # 目标机也不用装任何运行时。评委双击就能跑。
  target_link_options(easel PUBLIC -static -static-libgcc -static-libstdc++)
endif()

# ---- Debug 预设的越界检查（D-24 第一批工具）--------------------------------
# 这些只在 Debug 下开，学生看到的报错立刻带位置。
target_compile_definitions(easel PUBLIC
  $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>>:_GLIBCXX_ASSERTIONS>)
if(EASEL_ENABLE_ASAN AND NOT MSVC)
  target_compile_options(easel PUBLIC -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(easel PUBLIC -fsanitize=address)
endif()

# 把 assets/ 复制到构建目录，程序不用管当前目录在哪
add_custom_command(TARGET easel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:easel>/assets
  COMMENT "复制 assets/")

# ============================================================================
#  安装：产出一份可搬迁的预编译包（D-26 绿色工具箱）
#
#  目的：学生工程别再从源码编 ImGui/GLFW（首次构建 3 分钟）。装好之后
#      find_package(easel CONFIG REQUIRED) + target_link_libraries(app PRIVATE easel::easel)
#  几秒钟就链上。
#
#  布局（<prefix> = <工具箱>/easel/prebuilt）：
#      include/            easel/*.h + imgui.h/implot.h（摊平）+ GLFW/ + nlohmann/ + doctest/ + nfd.h
#      lib/                libeasel.a libeasel_imgui.a libglfw3.a libnfd.a
#      lib/cmake/easel/    easelConfig.cmake / easelConfigVersion.cmake / easelTargets.cmake
#      lib/cmake/glfw3/    lib/cmake/nfd/  lib/cmake/doctest/  share/cmake/nlohmann_json/
#
#  可搬迁：config 里一个绝对路径都没有，全靠 ${CMAKE_CURRENT_LIST_DIR} 往上推。
#  模板 / examples / docs / dist 不进包 —— 那些由 scripts/make_toolbox.py 从源码树直接拷。
# ============================================================================
if(EASEL_INSTALL)
  include(CMakePackageConfigHelpers)
  # 故意不用 GNUInstallDirs：三平台统一 include/ 和 lib/，学生和脚本一眼能看懂路径。
  # （glfw/doctest 在 Linux 上会自己走 GNUInstallDirs 去 lib64/，find_package 照样找得到。）
  set(EASEL_INSTALL_CMAKEDIR "lib/cmake/easel")

  # ---- 头文件 --------------------------------------------------------------
  install(DIRECTORY include/ DESTINATION include)          # easel/*.h
  # ImGui / ImPlot：D-08 的铁律是学生能直接 #include <imgui.h>，所以摊平装进 include/。
  # 私有依赖（stb、miniaudio、ImGuiColorTextEdit）已经编进 .a 里了，头一律不装。
  set(_easel_imgui_headers
      ${imgui_SOURCE_DIR}/imgui.h
      ${imgui_SOURCE_DIR}/imconfig.h
      ${imgui_SOURCE_DIR}/imgui_internal.h
      ${imgui_SOURCE_DIR}/imstb_rectpack.h
      ${imgui_SOURCE_DIR}/imstb_textedit.h
      ${imgui_SOURCE_DIR}/imstb_truetype.h
      ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.h
      ${implot_SOURCE_DIR}/implot.h
      ${implot_SOURCE_DIR}/implot_internal.h)
  if(EASEL_BACKEND_RESOLVED STREQUAL "dx11")
    list(APPEND _easel_imgui_headers ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.h)
  else()
    list(APPEND _easel_imgui_headers
         ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.h
         ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3_loader.h)
  endif()
  install(FILES ${_easel_imgui_headers} DESTINATION include)

  # ---- 两个静态库 ----------------------------------------------------------
  # easel_imgui 是 easel 的 PRIVATE 依赖，但静态库的 PRIVATE 依赖仍然要出现在
  # 链接接口里（$<LINK_ONLY:...>），所以它也必须进同一个 export set。
  install(TARGETS easel easel_imgui
          EXPORT easelTargets
          ARCHIVE  DESTINATION lib
          LIBRARY  DESTINATION lib
          RUNTIME  DESTINATION bin
          INCLUDES DESTINATION include)
  install(EXPORT easelTargets
          FILE      easelTargets.cmake
          NAMESPACE easel::
          DESTINATION ${EASEL_INSTALL_CMAKEDIR})

  # ---- easelConfig.cmake ---------------------------------------------------
  configure_package_config_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/cmake/easelConfig.cmake.in
      ${CMAKE_CURRENT_BINARY_DIR}/easelConfig.cmake
      INSTALL_DESTINATION ${EASEL_INSTALL_CMAKEDIR})
  # 0.x 阶段小版本之间不保证兼容，用 SameMinorVersion
  write_basic_package_version_file(
      ${CMAKE_CURRENT_BINARY_DIR}/easelConfigVersion.cmake
      VERSION       ${PROJECT_VERSION}
      COMPATIBILITY SameMinorVersion)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/easelConfig.cmake
                ${CMAKE_CURRENT_BINARY_DIR}/easelConfigVersion.cmake
          DESTINATION ${EASEL_INSTALL_CMAKEDIR})
endif()

# ============================================================================
#  示例与测试
# ============================================================================
if(EASEL_BUILD_EXAMPLES)
  add_subdirectory(examples/sort)
  add_subdirectory(examples/creative)
endif()

# ---- 工作台（D-29）：学生的入口。编译、运行、停止、生成 exe、导出源码 ----------
# 只在单独构建 Easel 时编（学生工程 add_subdirectory 进来时不该多编一个工具）
option(EASEL_BUILD_WORKBENCH "编译工作台" ${EASEL_IS_TOP})
if(EASEL_BUILD_WORKBENCH)
  add_executable(workbench workbench/main.cpp)
  target_link_libraries(workbench PRIVATE easel easel_imgui nfd)
  target_include_directories(workbench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(workbench PRIVATE EASEL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  if(MSVC)
    target_compile_options(workbench PRIVATE /utf-8)
  endif()
endif()

if(EASEL_BUILD_TESTS)
  enable_testing()
  add_executable(easel_tests tests/test_core.cpp tests/test_editor.cpp tests/test_canvas.cpp
                             tests/test_audio.cpp)
  target_link_libraries(easel_tests PRIVATE easel doctest::doctest)
  if(MSVC)
    target_compile_options(easel_tests PRIVATE /utf-8)
  endif()
  add_test(NAME easel_core COMMAND easel_tests)

  # 铁律：core.h 必须能被裸 g++ 编译（D-23 第 1 条）。CI 也跑这个。
  add_test(NAME easel_amalgamate_is_fresh
           COMMAND ${CMAKE_COMMAND} -E env python3
                   ${CMAKE_CURRENT_SOURCE_DIR}/scripts/amalgamate.py --check)
endif()
