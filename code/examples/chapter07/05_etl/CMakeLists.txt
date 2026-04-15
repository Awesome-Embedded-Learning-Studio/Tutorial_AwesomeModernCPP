cmake_minimum_required(VERSION 3.20)
project(chapter07_05_etl CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Embedded-friendly options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# Add ETL include path (if you have ETL installed)
# include_directories(/path/to/etl/include)

# Note: This example requires the ETL library
# You can get it from: https://github.com/ETLCPP/etl

# Create executables for each example
add_executable(etl_vector_demo etl_vector_demo.cpp)
add_executable(etl_queue_demo etl_queue_demo.cpp)
add_executable(etl_pool_demo etl_pool_demo.cpp)

# Optional: disable exceptions and RTTI for embedded
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")
