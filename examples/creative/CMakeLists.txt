add_executable(easel_example_creative main.cpp)
target_link_libraries(easel_example_creative PRIVATE easel::easel)
if(MSVC)
  target_compile_options(easel_example_creative PRIVATE /utf-8)
endif()
if(WIN32)
  set_target_properties(easel_example_creative PROPERTIES WIN32_EXECUTABLE TRUE)   # 没有黑框；printf 进 F12 日志窗，命令行下 Easel 会接回父终端
  if(MSVC)
    target_link_options(easel_example_creative PRIVATE "/ENTRY:mainCRTStartup")
  endif()
endif()

# 素材拷到 exe 旁边，双击也能跑（没有 assets/ 就跳过）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets)
  add_custom_command(TARGET easel_example_creative POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:easel_example_creative>/assets)
endif()
