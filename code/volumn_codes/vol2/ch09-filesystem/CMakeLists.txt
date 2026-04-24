cmake_minimum_required(VERSION 3.20)
project(ch09_filesystem VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 01-filesystem-path.md
add_executable(path_construct path_construct.cpp)
add_executable(path_decompose path_decompose.cpp)
add_executable(path_find_by_extension path_find_by_extension.cpp)

# 02-filesystem-ops.md
add_executable(ops_exists ops_exists.cpp)
add_executable(ops_file_info ops_file_info.cpp)
add_executable(ops_log_rotation ops_log_rotation.cpp)

# 03-directory-iteration.md
add_executable(dir_iter_basic dir_iter_basic.cpp)
add_executable(dir_iter_search dir_iter_search.cpp)
add_executable(dir_iter_code_stats dir_iter_code_stats.cpp)
