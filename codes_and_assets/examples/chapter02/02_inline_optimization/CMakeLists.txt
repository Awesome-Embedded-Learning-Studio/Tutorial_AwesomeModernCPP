cmake_minimum_required(VERSION 3.20)
project(chapter02_02_inline_optimization CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 为每个示例创建可执行文件
add_executable(inline_basics inline_basics.cpp)
add_executable(inline_vs_macro inline_vs_macro.cpp)
add_executable(code_bloat code_bloat.cpp)
add_executable(constexpr_vs_inline constexpr_vs_inline.cpp)
