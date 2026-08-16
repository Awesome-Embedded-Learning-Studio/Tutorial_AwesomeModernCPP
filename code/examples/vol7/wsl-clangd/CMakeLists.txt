cmake_minimum_required(VERSION 3.20)
project(greeter LANGUAGES CXX)

# 把每个 .cpp 的完整编译命令导出到 build/compile_commands.json,
# clangd 拿到这份"翻译说明书"才能精确还原每个文件的编译视角
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(greeter main.cpp greet.cpp)
target_include_directories(greeter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
