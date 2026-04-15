cmake_minimum_required(VERSION 3.20)
project(chapter02_04_crtp_polymorphism CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 为每个示例创建可执行文件
add_executable(runtime_polymorphism runtime_polymorphism.cpp)
add_executable(compile_time_polymorphism compile_time_polymorphism.cpp)
add_executable(crtp_example crtp_example.cpp)
add_executable(variant_example variant_example.cpp)
