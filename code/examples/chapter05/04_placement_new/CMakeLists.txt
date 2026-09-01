cmake_minimum_required(VERSION 3.20)
project(chapter05_04_placement_new CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 警告旗标按编译器分派:MSVC 用 /W4,GCC/Clang 用 -Wall -Wextra(Windows 免 WSL 直接构建)
add_compile_options(
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra>"
)

# 为每个示例创建可执行文件
add_executable(basic_placement_new basic_placement_new.cpp)
add_executable(bump_allocator bump_allocator.cpp)
add_executable(inplace_wrapper inplace_wrapper.cpp)
