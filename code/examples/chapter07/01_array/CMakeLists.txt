cmake_minimum_required(VERSION 3.20)
project(chapter07_01_array CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Embedded-friendly options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# Create executables for each example
add_executable(basic_usage basic_usage.cpp)
add_executable(static_storage static_storage.cpp)
add_executable(dma_usage dma_usage.cpp)
add_executable(constexpr_table constexpr_table.cpp)
add_executable(constexpr_crc_table constexpr_crc_table.cpp)
add_executable(structured_binding structured_binding.cpp)
add_executable(c_api_compatible c_api_compatible.cpp)

# Optional: disable exceptions and RTTI for embedded
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")
