cmake_minimum_required(VERSION 3.20)
project(Vol1Ch00 VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(calc calc.cpp)
add_executable(hello_world hello_world.cpp)
add_executable(setup-linux setup-linux.cpp)
add_executable(setup-windows setup-windows.cpp)
