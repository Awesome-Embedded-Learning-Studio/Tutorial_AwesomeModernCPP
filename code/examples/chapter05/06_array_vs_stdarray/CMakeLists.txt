cmake_minimum_required(VERSION 3.20)
project(chapter05_06_array_vs_stdarray CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 为每个示例创建可执行文件
add_executable(basic_comparison basic_comparison.cpp)
add_executable(practical_examples practical_examples.cpp)
