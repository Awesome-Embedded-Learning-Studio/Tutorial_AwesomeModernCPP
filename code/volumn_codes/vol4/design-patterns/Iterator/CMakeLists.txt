cmake_minimum_required(VERSION 3.10)
# 默认 Debug(本地调试);外部显式指定(如 MSVC 线 -DCMAKE_BUILD_TYPE=Release)
# 时不覆盖——写死 set() 会盖掉命令行传参,MSVC 下 Debug 的 /RTC1 与示例的
# /O2 冲突(D8016)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug)
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(Iterator VERSION 0.1.0 LANGUAGES C CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

add_executable(BinaryTree BinaryTree.h BinaryTreeMain.cpp)
target_compile_options(BinaryTree PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/W4;/O2>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-O2>")
