cmake_minimum_required(VERSION 3.20)
project(ch04_concurrent_data_structures VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(01_bounded_queue 01_bounded_queue.cpp)
target_link_libraries(01_bounded_queue PRIVATE Threads::Threads)

add_executable(02_lock_free_basics 02_lock_free_basics.cpp)
target_link_libraries(02_lock_free_basics PRIVATE Threads::Threads)

add_executable(03_spsc_ring_buffer 03_spsc_ring_buffer.cpp)
target_link_libraries(03_spsc_ring_buffer PRIVATE Threads::Threads)
