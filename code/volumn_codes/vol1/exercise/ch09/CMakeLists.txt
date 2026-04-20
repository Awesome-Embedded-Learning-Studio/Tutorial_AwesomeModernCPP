cmake_minimum_required(VERSION 3.20)
project(Vol1ExerciseCh09 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ex01_find_index ex01_find_index.cpp)
add_executable(ex02_bubble_sort ex02_bubble_sort.cpp)
add_executable(ex03_accumulate_all ex03_accumulate_all.cpp)
add_executable(ex04_pair ex04_pair.cpp)
add_executable(ex05_ringbuffer ex05_ringbuffer.cpp)
add_executable(ex06_serializer ex06_serializer.cpp)
add_executable(ex07_wrapper ex07_wrapper.cpp)
