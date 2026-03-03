cmake_minimum_required(VERSION 3.15)
project(optional_examples CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(optional_basics optional_basics.cpp)
add_executable(optional_parse optional_parse.cpp)
add_executable(optional_chain optional_chain.cpp)
