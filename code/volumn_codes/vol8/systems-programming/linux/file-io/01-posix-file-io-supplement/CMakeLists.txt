cmake_minimum_required(VERSION 3.14)
project(posix-file-io-supplement-experiments LANGUAGES CXX)

# 这些实验依赖 Linux 专属接口(fcntl F_DUPFD/F_SETFL / dup3 / SEEK_DATA/SEEK_HOLE /
# fork+exec / mkfifo / /proc/self/exe),别在别处构建
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "本目录实验仅支持 Linux(WSL2 可),请在 Linux 环境下构建")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COMMON_FLAGS -Wall -Wextra -Wpedantic -O2 -g)

# 目录按实验编号;数据文件路径烧死在各源码里(~/l01b_scratch/eN),复跑前先 mkdir
add_executable(e1_fcntl 01-fcntl/fcntl_flags.cpp)
add_executable(e2_dup   02-dup/dup_family.cpp)
add_executable(e3_seek  03-seek/seek_family.cpp)

foreach(t e1_fcntl e2_dup e3_seek)
    target_compile_options(${t} PRIVATE ${COMMON_FLAGS})
    target_include_directories(${t} PRIVATE common)
endforeach()
