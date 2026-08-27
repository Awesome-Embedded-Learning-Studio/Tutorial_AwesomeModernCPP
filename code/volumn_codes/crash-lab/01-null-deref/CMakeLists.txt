cmake_minimum_required(VERSION 3.16)
project(01_null_deref CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 复现代码 ---
add_executable(crash crash.cpp)
target_compile_options(crash PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4 /Od /Zi>
                                     $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -g -O0>)

# --- 修复代码 ---
add_executable(fixed fixed.cpp)
target_compile_options(fixed PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4 /Od /Zi>
                                     $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -g -O0>)
