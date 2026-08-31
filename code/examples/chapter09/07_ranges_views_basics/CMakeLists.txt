cmake_minimum_required(VERSION 3.15)
project(chapter09_ranges_views CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# MSVC 线用 /W4 对齐 GCC 线警告水位(不开 /WX,能跑优先);必须在 add_executable 之前
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/W4>)
add_executable(view_basics view_basics.cpp)
add_executable(filter_views filter_views.cpp)
add_executable(transform_views transform_views.cpp)
add_executable(take_drop_views take_drop_views.cpp)
add_executable(split_view split_view.cpp)
add_executable(sensor_pipeline sensor_pipeline.cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(view_basics PRIVATE -Wall -Wextra -Werror)
    target_compile_options(filter_views PRIVATE -Wall -Wextra -Werror)
    target_compile_options(transform_views PRIVATE -Wall -Wextra -Werror)
    target_compile_options(take_drop_views PRIVATE -Wall -Wextra -Werror)
    target_compile_options(split_view PRIVATE -Wall -Wextra -Werror)
    target_compile_options(sensor_pipeline PRIVATE -Wall -Wextra -Werror)
endif()
