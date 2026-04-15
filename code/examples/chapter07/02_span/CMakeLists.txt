cmake_minimum_required(VERSION 3.20)
project(chapter07_02_span CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Embedded-friendly options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# Create executables for each example
add_executable(basic_usage basic_usage.cpp)
add_executable(static_extent static_extent.cpp)
add_executable(subspan_example subspan_example.cpp)
add_executable(packet_parsing packet_parsing.cpp)
add_executable(bytes_view bytes_view.cpp)
add_executable(function_parameter function_parameter.cpp)

# Optional: disable exceptions and RTTI for embedded
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")
