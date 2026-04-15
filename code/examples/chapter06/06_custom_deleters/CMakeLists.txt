cmake_minimum_required(VERSION 3.20)
project(chapter06_06_custom_deleters CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

add_executable(file_handle file_handle.cpp)
add_executable(memory_pool memory_pool.cpp)
