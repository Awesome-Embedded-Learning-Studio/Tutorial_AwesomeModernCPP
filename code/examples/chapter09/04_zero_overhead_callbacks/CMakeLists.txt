cmake_minimum_required(VERSION 3.15)
project(chapter09_zero_overhead_callbacks CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# MSVC 线用 /W4 对齐 GCC 线警告水位(不开 /WX,能跑优先);必须在 add_executable 之前
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/W4>)
add_executable(zero_callback zero_callback.cpp)
add_executable(small_callback small_callback.cpp)
add_executable(fast_callback fast_callback.cpp)
add_executable(event_system event_system.cpp)
add_executable(multicast multicast.cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(zero_callback PRIVATE -Wall -Wextra -Werror -O2)
    target_compile_options(small_callback PRIVATE -Wall -Wextra -Werror -O2)
    target_compile_options(fast_callback PRIVATE -Wall -Wextra -Werror -O2)
    target_compile_options(event_system PRIVATE -Wall -Wextra -Werror)
    target_compile_options(multicast PRIVATE -Wall -Wextra -Werror)
endif()
