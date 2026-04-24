cmake_minimum_required(VERSION 3.20)
project(ch06_auto_decltype VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ctad_strong_typedef ctad_strong_typedef.cpp)
