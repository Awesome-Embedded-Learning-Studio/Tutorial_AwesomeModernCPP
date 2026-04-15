cmake_minimum_required(VERSION 3.20)
project(chapter07_04_intrusive_container CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Embedded-friendly options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# Create executables for each example
add_executable(inheritance_example inheritance_example.cpp)
add_executable(member_hook_example member_hook_example.cpp)
add_executable(task_scheduler_example task_scheduler_example.cpp)

# Optional: disable exceptions and RTTI for embedded
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")
