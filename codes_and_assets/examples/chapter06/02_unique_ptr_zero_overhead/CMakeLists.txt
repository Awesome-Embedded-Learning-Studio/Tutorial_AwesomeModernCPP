cmake_minimum_required(VERSION 3.20)
project(chapter06_02_unique_ptr_zero_overhead CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 嵌入式友好选项
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 为每个示例创建可执行文件
add_executable(basic_usage basic_usage.cpp)
add_executable(custom_deleter custom_deleter.cpp)
add_executable(polymorphism polymorphism.cpp)
