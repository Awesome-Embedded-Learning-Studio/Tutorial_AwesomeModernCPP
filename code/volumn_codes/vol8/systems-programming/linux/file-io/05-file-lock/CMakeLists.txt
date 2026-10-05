cmake_minimum_required(VERSION 3.14)
project(file-lock-experiments LANGUAGES CXX)

# 这些实验依赖 Linux 专属接口(flock / fcntl 记录锁 / fork / exec / procfs),别在别处构建
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "本目录实验仅支持 Linux(WSL2 可),请在 Linux 环境下构建")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COMMON_FLAGS -Wall -Wextra -Wpedantic -O2 -g)

# 目录按实验编号;数据文件路径烧死在各源码开头(~/l05_scratch/eN),复跑前先 mkdir
add_executable(e1_flock     01-flock-matrix/flock_matrix.cpp)
add_executable(e2_fcntl     02-fcntl-matrix/fcntl_matrix.cpp)
add_executable(e3_locks     03-proc-locks/proc_locks.cpp)
add_executable(e4_inherit   04-flock-vs-fcntl/inheritance.cpp)
add_executable(e5_raii      05-raii-file-lock/raii_demo.cpp)
add_executable(e6_content   06-contention-bench/contention.cpp)
add_executable(e7_fsprobe   07-fs-compare/fs_probe.cpp)

foreach(t e1_flock e2_fcntl e3_locks e4_inherit e5_raii e6_content e7_fsprobe)
    target_compile_options(${t} PRIVATE ${COMMON_FLAGS})
    target_include_directories(${t} PRIVATE common)
endforeach()

# e5 的 file_lock.hpp 与 demo 同目录
target_include_directories(e5_raii PRIVATE 05-raii-file-lock)
