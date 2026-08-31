cmake_minimum_required(VERSION 3.16)
project(01_null_deref CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 复现代码 ---
add_executable(crash crash.cpp)
target_compile_options(crash PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4 /Od /Zi>
                                     $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -g -O0>)

# --- 修复代码 ---
add_executable(fixed fixed.cpp)
target_compile_options(fixed PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4 /Od /Zi>
                                     $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -g -O0>)
