cmake_minimum_required(VERSION 3.15)
project(literal_operators_examples CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(time_units time_units.cpp)
add_executable(frequency_units frequency_units.cpp)
add_executable(voltage_units voltage_units.cpp)
