cmake_minimum_required(VERSION 3.20)
project(chapter03_03_rvo_nrvo CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

add_executable(rvo_nrvo_example rvo_nrvo_example.cpp)
