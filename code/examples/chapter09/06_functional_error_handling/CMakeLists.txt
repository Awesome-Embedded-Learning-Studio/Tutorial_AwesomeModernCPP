cmake_minimum_required(VERSION 3.15)
project(chapter09_functional_error_handling CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# MSVC 线用 /W4 对齐 GCC 线警告水位(不开 /WX,能跑优先);必须在 add_executable 之前
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/W4>)
add_executable(basic_result basic_result.cpp)
add_executable(combinators combinators.cpp)
add_executable(config_loader config_loader.cpp)
add_executable(retry retry.cpp)
add_executable(peripheral_init peripheral_init.cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(basic_result PRIVATE -Wall -Wextra -Werror)
    target_compile_options(combinators PRIVATE -Wall -Wextra -Werror)
    target_compile_options(config_loader PRIVATE -Wall -Wextra -Werror)
    target_compile_options(retry PRIVATE -Wall -Wextra -Werror)
    target_compile_options(peripheral_init PRIVATE -Wall -Wextra -Werror)
endif()
