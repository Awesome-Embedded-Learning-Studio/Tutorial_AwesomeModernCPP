cmake_minimum_required(VERSION 3.20)
project(greeter LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 把每个 .cpp 的完整编译命令导出到 build/compile_commands.json,
# clangd 拿到这份"翻译说明书"才能精确还原每个文件的编译视角
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(greeter main.cpp greet.cpp)
target_include_directories(greeter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
