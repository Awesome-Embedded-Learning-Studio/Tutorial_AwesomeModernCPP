cmake_minimum_required(VERSION 3.15)
project(chapter09_ranges_pipes CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# MSVC 线用 /W4 对齐 GCC 线警告水位(不开 /WX,能跑优先);必须在 add_executable 之前
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/W4>)
add_executable(pipeline_basics pipeline_basics.cpp)
add_executable(adc_pipeline adc_pipeline.cpp)
add_executable(protocol_parser protocol_parser.cpp)
set_target_properties(protocol_parser PROPERTIES CXX_STANDARD 23)
add_executable(event_manager event_manager.cpp)
add_executable(ring_buffer_pipeline ring_buffer_pipeline.cpp)
add_executable(performance_compare performance_compare.cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pipeline_basics PRIVATE -Wall -Wextra -Werror -O2)
    target_compile_options(adc_pipeline PRIVATE -Wall -Wextra -Werror)
    target_compile_options(protocol_parser PRIVATE -Wall -Wextra -Werror)
    target_compile_options(event_manager PRIVATE -Wall -Wextra -Werror)
    target_compile_options(ring_buffer_pipeline PRIVATE -Wall -Wextra -Werror)
    target_compile_options(performance_compare PRIVATE -Wall -Wextra -Werror -O2)
endif()
