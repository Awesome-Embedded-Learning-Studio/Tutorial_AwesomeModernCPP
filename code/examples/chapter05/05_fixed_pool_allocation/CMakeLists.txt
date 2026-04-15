cmake_minimum_required(VERSION 3.20)
project(chapter05_05_fixed_pool_allocation CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 为每个示例创建可执行文件
add_executable(fixed_pool fixed_pool.cpp)
add_executable(slab_allocator slab_allocator.cpp)
