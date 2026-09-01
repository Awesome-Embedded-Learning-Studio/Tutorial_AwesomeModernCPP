cmake_minimum_required(VERSION 3.20)
project(Vol1Ch11 VERSION 0.0.1 LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(vector_task_manager vector_task_manager.cpp)
add_executable(map_basics map_basics.cpp)
add_executable(set_basics set_basics.cpp)
add_executable(unordered_map_demo unordered_map_demo.cpp)
add_executable(map_set_word_freq map_set_word_freq.cpp)
add_executable(algo_student_scores algo_student_scores.cpp)
add_executable(stl_sensor_analysis stl_sensor_analysis.cpp)
