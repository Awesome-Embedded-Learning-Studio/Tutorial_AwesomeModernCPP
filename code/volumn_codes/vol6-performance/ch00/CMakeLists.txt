cmake_minimum_required(VERSION 3.20)
project(vol6_ch00_performance_mindset LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ch00-01 命题验证:vector+二分 vs set+find,同为 O(log n) 的缓存效应对比
add_executable(vector_vs_set vector_vs_set.cpp)
target_compile_options(vector_vs_set PRIVATE -Wall -Wextra -Wpedantic -O2)
