cmake_minimum_required(VERSION 3.20)
project(lesson_06_scope C)

add_subdirectory(ex1_counter_module)
add_subdirectory(ex2_multi_file_visibility)
add_executable(ex3_config_singleton ex3_config_singleton.c)
