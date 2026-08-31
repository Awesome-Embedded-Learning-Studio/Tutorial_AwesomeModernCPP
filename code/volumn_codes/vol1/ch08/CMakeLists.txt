cmake_minimum_required(VERSION 3.20)
project(Vol1Ch08 VERSION 0.0.1 LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(01-single-inheritance 01-single-inheritance.cpp)
add_executable(02a-virtual-functions 02a-virtual-functions.cpp)
add_executable(02b-virtual-functions 02b-virtual-functions.cpp)
add_executable(03-abstract-classes 03-abstract-classes.cpp)
add_executable(04-multiple-inheritance 04-multiple-inheritance.cpp)
add_executable(05-oop-in-practice 05-oop-in-practice.cpp)
