cmake_minimum_required(VERSION 3.15)
project(variant_examples CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(variant_basics variant_basics.cpp)
add_executable(variant_visit variant_visit.cpp)
add_executable(variant_message variant_message.cpp)
