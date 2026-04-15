cmake_minimum_required(VERSION 3.20)
project(chapter06_08_scope_guard_patterns CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

add_executable(scope_guard scope_guard.cpp)
add_executable(scope_success_fail scope_success_fail.cpp)
