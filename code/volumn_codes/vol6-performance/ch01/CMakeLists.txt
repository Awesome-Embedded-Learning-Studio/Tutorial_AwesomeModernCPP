cmake_minimum_required(VERSION 3.20)
project(vol6_ch01_benchmark_methodology CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# FetchContent 拉 Google Benchmark —— reader 不用预装,clone 仓库就能跑
include(FetchContent)
FetchContent_Declare(benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.5)
# 关掉 benchmark 自己的测试目标(注意 flag 名是 BENCHMARK_ENABLE_TESTING,
# 不是 BENCHMARK_ENABLE_TESTS——写错会去 build 它的内部测试、缺 gtest 就炸)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

# ch01-02 最小完整例子:push_back + DoNotOptimize/ClobberMemory + 参数扫描 + 重复聚合
add_executable(push_bench push_bench.cpp)
target_link_libraries(push_bench PRIVATE benchmark::benchmark_main)
target_compile_options(push_bench PRIVATE -O2 -Wall -Wextra -Wpedantic)
