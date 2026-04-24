cmake_minimum_required(VERSION 3.15)
project(udl_examples CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 添加编译警告
add_compile_options(-Wall -Wextra -Wpedantic)

# 示例 1: UDL 基础
add_executable(01_udl_basics 01-udl-basics.cpp)

# 示例 2: UDL 实战
add_executable(02_udl_practice 02-udl-practice.cpp)

# 安装目标（可选）
install(TARGETS 01_udl_basics 02_udl_practice
        RUNTIME DESTINATION bin)
