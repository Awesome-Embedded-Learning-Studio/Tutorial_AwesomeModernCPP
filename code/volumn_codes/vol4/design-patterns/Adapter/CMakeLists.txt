cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(Adapter VERSION 0.1.0 LANGUAGES C CXX)

add_executable(AdapterDemo Adapter.h AdapterMain.cpp)

target_compile_options(AdapterDemo PRIVATE -Wall -Wextra -Wpedantic)
