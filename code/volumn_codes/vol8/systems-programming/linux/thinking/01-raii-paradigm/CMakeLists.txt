cmake_minimum_required(VERSION 3.16)
project(raii_paradigm CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(common)

add_executable(strace_demo 01-strace/strace_demo.cpp)
add_executable(leak_raw 02-leak/leak_raw.cpp)
add_executable(leak_raii 02-leak/leak_raii.cpp)
add_executable(release_reset_swap 03-release/release_reset_swap.cpp)

add_executable(vector_noexcept 04-vector/vector_demo.cpp)
add_executable(vector_throwing 04-vector/vector_demo.cpp)
target_compile_definitions(vector_throwing PRIVATE RAII_MOVE_MAY_THROW)

# 复现 strace 存档(strace 7.2 实测):
#   strace -f -e trace=openat,close -o strace_fd.out ./strace_demo
#   strace -f -e trace=mmap,munmap -o strace_mmap.out ./strace_demo
#   strace -f -e trace=openat,close ./release_reset_swap 2>&1
