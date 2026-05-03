cmake_minimum_required(VERSION 3.20)

project(ChromeBase LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CPM.cmake)

message("Processing the OnceCallback")
add_subdirectory(once_callback)

# Tests
enable_testing()
add_subdirectory(test)
