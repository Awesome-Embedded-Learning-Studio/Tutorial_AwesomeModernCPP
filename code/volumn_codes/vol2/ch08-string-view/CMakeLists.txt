cmake_minimum_required(VERSION 3.20)
project(ch08_string_view VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(parse_kv parse_kv.cpp)
add_executable(split_tokens split_tokens.cpp)
add_executable(bench_substr bench_substr.cpp)
add_executable(dangling_view_ub dangling_view_ub.cpp)
