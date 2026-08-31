cmake_minimum_required(VERSION 3.20)
project(ch08_string_view VERSION 0.0.1 LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(parse_kv parse_kv.cpp)
add_executable(split_tokens split_tokens.cpp)
add_executable(bench_substr bench_substr.cpp)
add_executable(dangling_view_ub dangling_view_ub.cpp)
