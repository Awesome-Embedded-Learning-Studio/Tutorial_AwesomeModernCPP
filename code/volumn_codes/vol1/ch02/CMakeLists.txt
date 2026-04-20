cmake_minimum_required(VERSION 3.20)
project(Vol1Ch02 VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(conditionals_basic conditionals_basic.cpp)
add_executable(conditional conditional.cpp)
add_executable(while_sum while_sum.cpp)
add_executable(multiplication_table multiplication_table.cpp)
add_executable(loops loops.cpp)
add_executable(range_for_intro range_for_intro.cpp)
add_executable(range_for range_for.cpp)
