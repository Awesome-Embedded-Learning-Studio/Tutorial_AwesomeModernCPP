cmake_minimum_required(VERSION 3.20)
project(chapter10_05_interrupt_safe CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 嵌入式友好选项
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 为每个示例创建可执行文件
add_executable(01_isr_basics 01_isr_basics.cpp)
add_executable(02_atomic_in_isr 02_atomic_in_isr.cpp)
add_executable(03_memory_barrier 03_memory_barrier.cpp)
add_executable(04_lockfree_queue_isr 04_lockfree_queue_isr.cpp)
add_executable(05_volatile_vs_atomic 05_volatile_vs_atomic.cpp)
