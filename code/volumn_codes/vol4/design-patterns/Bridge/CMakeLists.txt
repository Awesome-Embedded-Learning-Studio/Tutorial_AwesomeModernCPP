cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(Bridge VERSION 0.1.0 LANGUAGES C CXX)

add_executable(BridgeDemo
    Bridge.h
    Widget.h
    Widget.cpp
    BridgeMain.cpp
)

target_compile_options(BridgeDemo PRIVATE -Wall -Wextra -Wpedantic)
