cmake_minimum_required(VERSION 3.15)
project(type_aliases_examples CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(using_basics using_basics.cpp)
add_executable(using_templates using_templates.cpp)
add_executable(using_register using_register.cpp)
