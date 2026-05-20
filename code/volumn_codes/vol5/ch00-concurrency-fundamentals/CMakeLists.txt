cmake_minimum_required(VERSION 3.20)
project(ch00_concurrency_fundamentals VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(false_sharing_bench false_sharing_bench.cpp)
target_link_libraries(false_sharing_bench PRIVATE Threads::Threads)
