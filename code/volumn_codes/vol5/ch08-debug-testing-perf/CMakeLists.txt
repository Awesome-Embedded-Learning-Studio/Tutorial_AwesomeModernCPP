cmake_minimum_required(VERSION 3.20)
project(ch08_debug_testing_perf VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(01_data_race_and_deadlock 01_data_race_and_deadlock.cpp)
target_link_libraries(01_data_race_and_deadlock PRIVATE Threads::Threads)

add_executable(02_sync_strategy_benchmark 02_sync_strategy_benchmark.cpp)
target_link_libraries(02_sync_strategy_benchmark PRIVATE Threads::Threads)
