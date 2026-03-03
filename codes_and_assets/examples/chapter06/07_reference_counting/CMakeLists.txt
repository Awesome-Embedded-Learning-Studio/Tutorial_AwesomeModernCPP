cmake_minimum_required(VERSION 3.20)
project(chapter06_07_reference_counting CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

find_package(Threads REQUIRED)

add_executable(intrusive_refcount intrusive_refcount.cpp)
add_executable(atomic_refcount atomic_refcount.cpp)

target_link_libraries(atomic_refcount Threads::Threads)
