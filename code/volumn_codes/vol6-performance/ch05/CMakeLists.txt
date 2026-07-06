cmake_minimum_required(VERSION 3.20)
project(vol6_ch05_multicore_performance LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(Threads REQUIRED)

# 05-01 伪共享:同 cacheline vs alignas(64)
add_executable(false_sharing false_sharing.cpp)
target_link_libraries(false_sharing PRIVATE Threads::Threads)
target_compile_options(false_sharing PRIVATE -O2 -Wall -Wextra -Wpedantic)

# 05-02 扩展性曲线:1/2/4/8 线程(NUMA 在本机单节点测不了)
add_executable(scalability scalability.cpp)
target_link_libraries(scalability PRIVATE Threads::Threads)
target_compile_options(scalability PRIVATE -O2 -Wall -Wextra -Wpedantic)

# 05-03 同步开销:非原子 vs atomic(relaxed/seq_cst)vs mutex
add_executable(lock_cost lock_cost.cpp)
target_link_libraries(lock_cost PRIVATE Threads::Threads)
target_compile_options(lock_cost PRIVATE -O2 -Wall -Wextra -Wpedantic)
