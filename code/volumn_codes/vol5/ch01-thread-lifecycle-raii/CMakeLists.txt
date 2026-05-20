cmake_minimum_required(VERSION 3.20)
project(ch01_thread_lifecycle_raii VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(01_thread_creation 01_thread_creation.cpp)
target_link_libraries(01_thread_creation PRIVATE Threads::Threads)

add_executable(02_thread_arguments_lifetime 02_thread_arguments_lifetime.cpp)
target_link_libraries(02_thread_arguments_lifetime PRIVATE Threads::Threads)

add_executable(03_raii_thread_guard 03_raii_thread_guard.cpp)
target_link_libraries(03_raii_thread_guard PRIVATE Threads::Threads)

add_executable(04_thread_local_call_once 04_thread_local_call_once.cpp)
target_link_libraries(04_thread_local_call_once PRIVATE Threads::Threads)
