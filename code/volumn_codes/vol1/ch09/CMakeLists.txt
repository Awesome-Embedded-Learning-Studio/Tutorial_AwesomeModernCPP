cmake_minimum_required(VERSION 3.20)
project(Vol1Ch09 VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(01-function-templates 01-function-templates.cpp)
add_executable(01a-overload-resolution 01a-overload-resolution.cpp)
add_executable(02-class-template-stack 02-class-template-stack.cpp)
add_executable(03-template-specialization 03-template-specialization.cpp)
