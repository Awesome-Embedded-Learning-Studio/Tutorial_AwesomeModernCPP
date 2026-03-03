cmake_minimum_required(VERSION 3.20)
project(chapter05_03_object_pool_pattern CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 为每个示例创建可执行文件
add_executable(object_pool object_pool.cpp)
add_executable(specialized_pool specialized_pool.cpp)
