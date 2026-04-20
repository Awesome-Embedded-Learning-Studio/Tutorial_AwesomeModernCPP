cmake_minimum_required(VERSION 3.20)
project(Vol1Ch12 VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 01-memory-layout
add_executable(01-memory-segments     01-memory-segments.cpp)
add_executable(01-stack-allocation    01-stack-allocation.cpp)
add_executable(01-heap-allocation     01-heap-allocation.cpp)
add_executable(01-memory-layout       01-memory-layout.cpp)

# 02-new-delete
add_executable(02-smart-vs-raw        02-smart-vs-raw.cpp)
add_executable(02-leak-demo           02-leak-demo.cpp)
add_executable(02-dangling-pointer    02-dangling-pointer.cpp)

# 03-alignment-padding
add_executable(03-alignment           03-alignment.cpp)
