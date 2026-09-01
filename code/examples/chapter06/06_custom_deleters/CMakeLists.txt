cmake_minimum_required(VERSION 3.20)
project(chapter06_06_custom_deleters CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
# 警告旗标按编译器分派:MSVC 用 /W4,GCC/Clang 用 -Wall -Wextra(Windows 免 WSL 直接构建)
add_compile_options(
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra>"
)
# 示例演示 fopen/CRT fd API,MSVC 的 C4996 安全弃用告警不适用于教学语境
add_compile_definitions($<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)

add_executable(file_handle file_handle.cpp)
add_executable(memory_pool memory_pool.cpp)
