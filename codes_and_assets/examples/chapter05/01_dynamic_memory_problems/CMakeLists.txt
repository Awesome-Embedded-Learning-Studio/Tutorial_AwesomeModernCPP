cmake_minimum_required(VERSION 3.20)
project(chapter05_01_dynamic_memory_problems CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 为每个示例创建可执行文件
add_executable(memory_layout_demo memory_layout_demo.cpp)
add_executable(fragmentation_demo fragmentation_demo.cpp)
add_executable(alignment_demo alignment_demo.cpp)
