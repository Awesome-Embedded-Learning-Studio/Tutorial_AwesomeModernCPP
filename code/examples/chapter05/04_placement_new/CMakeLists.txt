cmake_minimum_required(VERSION 3.20)
project(chapter05_04_placement_new CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 为每个示例创建可执行文件
add_executable(basic_placement_new basic_placement_new.cpp)
add_executable(bump_allocator bump_allocator.cpp)
add_executable(inplace_wrapper inplace_wrapper.cpp)
