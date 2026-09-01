cmake_minimum_required(VERSION 3.15)
project(chapter09_lambda_capture_performance CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# MSVC 线用 /W4 对齐 GCC 线警告水位(不开 /WX,能跑优先);必须在 add_executable 之前
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/W4>)
add_executable(value_capture value_capture.cpp)
add_executable(reference_capture reference_capture.cpp)
add_executable(full_capture full_capture.cpp)
add_executable(init_capture init_capture.cpp)
add_executable(mutable_capture mutable_capture.cpp)
add_executable(performance_demo performance_demo.cpp)
add_executable(capture_size capture_size.cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(value_capture PRIVATE -Wall -Wextra -Werror)
    target_compile_options(reference_capture PRIVATE -Wall -Wextra -Werror)
    target_compile_options(full_capture PRIVATE -Wall -Wextra -Werror)
    target_compile_options(init_capture PRIVATE -Wall -Wextra -Werror)
    target_compile_options(mutable_capture PRIVATE -Wall -Wextra -Werror)
    target_compile_options(performance_demo PRIVATE -Wall -Wextra -Werror -O2)
    target_compile_options(capture_size PRIVATE -Wall -Wextra -Werror)
endif()
