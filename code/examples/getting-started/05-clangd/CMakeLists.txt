cmake_minimum_required(VERSION 3.20)
project(greeter LANGUAGES CXX)

# 让 CMake 在 build 目录生成 compile_commands.json，clangd 靠它看懂工程
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(greeter main.cpp greet.cpp)
