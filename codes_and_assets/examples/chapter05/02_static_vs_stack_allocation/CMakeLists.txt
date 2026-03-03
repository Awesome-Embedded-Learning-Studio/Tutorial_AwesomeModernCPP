cmake_minimum_required(VERSION 3.20)
project(chapter05_02_static_vs_stack_allocation CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 为每个示例创建可执行文件
add_executable(static_allocation_demo static_allocation_demo.cpp)
add_executable(stack_allocation_demo stack_allocation_demo.cpp)
add_executable(ring_buffer_demo ring_buffer_demo.cpp)
