cmake_minimum_required(VERSION 3.15)
project(type_aliases_examples CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(using_basics using_basics.cpp)
add_executable(using_templates using_templates.cpp)
add_executable(using_register using_register.cpp)
