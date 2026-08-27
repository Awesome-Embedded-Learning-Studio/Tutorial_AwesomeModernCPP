cmake_minimum_required(VERSION 3.16)
project(02_use_after_free CXX)

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

# --- ASAN 选项（可选，手动开启） ---
# cmake -DCMAKE_CXX_FLAGS="-fsanitize=address -g" ..
# MSVC: cmake -DCMAKE_CXX_FLAGS="/fsanitize=address /Zi" ..
