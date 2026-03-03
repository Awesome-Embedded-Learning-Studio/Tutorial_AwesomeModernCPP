cmake_minimum_required(VERSION 3.15)
project(enum_class_examples CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Examples
add_executable(enum_class_basics enum_class_basics.cpp)
add_executable(enum_class_bitflags enum_class_bitflags.cpp)
add_executable(enum_class_memory enum_class_memory.cpp)
