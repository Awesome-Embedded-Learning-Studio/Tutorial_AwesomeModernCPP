cmake_minimum_required(VERSION 3.20)
project(Vol1Ch10 VERSION 0.0.1 LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(basic_throw_catch basic_throw_catch.cpp)
add_executable(stack_unwinding stack_unwinding.cpp)
add_executable(exception_hierarchy exception_hierarchy.cpp)
add_executable(rethrow rethrow.cpp)
add_executable(exceptions exceptions.cpp)
add_executable(safety safety.cpp)
add_executable(error_cmp error_cmp.cpp)
set_target_properties(error_cmp PROPERTIES CXX_STANDARD 23)
