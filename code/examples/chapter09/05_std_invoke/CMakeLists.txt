cmake_minimum_required(VERSION 3.15)
project(chapter09_std_invoke CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# MSVC 线用 /W4 对齐 GCC 线警告水位(不开 /WX,能跑优先);必须在 add_executable 之前
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/W4>)
add_executable(invoke_basics invoke_basics.cpp)
add_executable(universal_caller universal_caller.cpp)
add_executable(wrapper wrapper.cpp)
add_executable(command_parser command_parser.cpp)
add_executable(invoke_result invoke_result.cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(invoke_basics PRIVATE -Wall -Wextra -Werror)
    target_compile_options(universal_caller PRIVATE -Wall -Wextra -Werror)
    target_compile_options(wrapper PRIVATE -Wall -Wextra -Werror)
    target_compile_options(command_parser PRIVATE -Wall -Wextra -Werror)
    target_compile_options(invoke_result PRIVATE -Wall -Wextra -Werror)
endif()
