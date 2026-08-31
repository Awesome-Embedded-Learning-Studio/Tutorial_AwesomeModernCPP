cmake_minimum_required(VERSION 3.20)
project(vol6_ch00_performance_mindset LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ch00-01 命题验证:vector+二分 vs set+find,同为 O(log n) 的缓存效应对比
add_executable(vector_vs_set vector_vs_set.cpp)
target_compile_options(vector_vs_set PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/W4;/O2>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-O2>")
