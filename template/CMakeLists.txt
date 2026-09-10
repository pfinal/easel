# ============================================================================
#  学生工程模板 —— 三个入口，一份业务代码（D-23）
#
#      solver   命令行程序    算法开发的主战场，和平时刷题一样
#      tests    doctest      对拍与回归
#      app      图形界面      可视化与交付
#
#  三个都 #include 同一个 src/solver.cpp。断点打在 solver.cpp 上，三个都能停。
#
#  构建：
#      cmake --preset default && cmake --build build/default
#  只想跑算法，连 CMake 都不用：
#      g++ -std=c++17 -DEASEL_STANDALONE src/solver.cpp -o solver && ./solver
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(my_project VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  add_compile_options(/utf-8)
endif()

# 所有可执行文件都放到 build/<preset>/bin/，这样 Mac 的 Ninja 和 Windows 的
# Visual Studio 生成器给出的路径是同一个 —— .vscode 里的 F5 配置才写得死。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
  string(TOUPPER ${cfg} CFG)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

include(FetchContent)

# ---- Easel ----------------------------------------------------------------
# 平时用 FetchContent 拉固定的 tag，不拷贝源码（D-13）。
# 想临时改 Easel 本身：cmake --preset default -DEASEL_DIR=../easel
# ---- 包里自带的 Easel  [easel:bundled] ----------------------------------
# 「导出工程」出来的包里有 easel/，直接用它：不联网、不用装、不用加 cmake 参数。
# 评委拿到的就是这条路径。平时开发时目录里没有 easel/，这一段什么都不做。
if(NOT DEFINED EASEL_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/easel/CMakeLists.txt")
  set(EASEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/easel")
  message(STATUS "用包里自带的 Easel: ${EASEL_DIR}")
endif()

if(DEFINED EASEL_DIR)
  message(STATUS "用本地的 Easel: ${EASEL_DIR}")
  add_subdirectory(${EASEL_DIR} ${CMAKE_BINARY_DIR}/easel-build)
else()
  FetchContent_Declare(easel
    GIT_REPOSITORY https://github.com/pfinal/easel.git
    GIT_TAG        v0.1.0
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(easel)
endif()

# doctest：Easel 已经带了一份，直接用就行（离线时这一步也不会联网）。
# 万一没有（比如你把 Easel 换成了别的），再自己拉一份。
if(NOT TARGET doctest::doctest)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.12
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(doctest)
endif()

# ---- 1. 命令行：算法开发的主战场 -------------------------------------------
add_executable(solver src/solver.cpp)
target_compile_definitions(solver PRIVATE EASEL_STANDALONE)
target_include_directories(solver PRIVATE src)

# ---- 2. 测试（有 tests/test_solver.cpp 才建；新工程默认不带，D-30）----------
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_solver.cpp)
  enable_testing()
  add_executable(tests tests/test_solver.cpp)
  target_link_libraries(tests PRIVATE doctest::doctest)
  target_include_directories(tests PRIVATE src)
  add_test(NAME solver_tests COMMAND tests)
endif()

# ---- 3. 图形界面 -----------------------------------------------------------
add_executable(app src/app.cpp)
target_link_libraries(app PRIVATE easel::easel)
target_include_directories(app PRIVATE src)
if(APPLE)
  set_target_properties(app PROPERTIES MACOSX_BUNDLE FALSE)
endif()
if(WIN32)
  # 交付时改成 TRUE 就没有黑框；开发阶段留 FALSE，能看到 printf。
  # 注意：Easel 会把 stdout 接到应用内的日志窗，所以就算改成 TRUE 也看得到输出。
  set_target_properties(app PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

# 数据和素材复制到 exe 旁边，双击也能跑（没有 assets/ 就跳过）
foreach(dir data assets)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
    add_custom_command(TARGET app POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${CMAKE_CURRENT_SOURCE_DIR}/${dir} $<TARGET_FILE_DIR:app>/${dir})
  endif()
endforeach()
