cmake_minimum_required(VERSION 3.15)
project(udl_examples CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 添加编译警告(MSVC 用 /W4)
add_compile_options(
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>")

# 示例 1: UDL 基础
add_executable(01_udl_basics 01-udl-basics.cpp)

# 示例 2: UDL 实战
add_executable(02_udl_practice 02-udl-practice.cpp)

# 安装目标（可选）
install(TARGETS 01_udl_basics 02_udl_practice
        RUNTIME DESTINATION bin)
