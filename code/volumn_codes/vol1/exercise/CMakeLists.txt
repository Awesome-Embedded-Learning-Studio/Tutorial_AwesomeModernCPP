cmake_minimum_required(VERSION 3.20)
project(Vol1Exercises CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(ch00)
add_subdirectory(ch01)
add_subdirectory(ch02)
add_subdirectory(ch03)
add_subdirectory(ch04)
add_subdirectory(ch05)
add_subdirectory(ch06)
add_subdirectory(ch07)
add_subdirectory(ch08)
add_subdirectory(ch09)
add_subdirectory(ch10)
add_subdirectory(ch11)
add_subdirectory(ch12)
