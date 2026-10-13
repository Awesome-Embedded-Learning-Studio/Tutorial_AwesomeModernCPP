cmake_minimum_required(VERSION 3.14)
project(page-cache-experiments LANGUAGES CXX)

# 这些实验依赖 Linux 专属接口(procfs / fork / utimensat / fsync),别在别处构建
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "本目录实验仅支持 Linux(WSL2 可),请在 Linux 环境下构建")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 源码自包含,不依赖 ../02-mmap-memory-mapping/common;计时路径全部走 clock_gettime
set(COMMON_FLAGS -Wall -Wextra -Wpedantic -O2 -g)

# 目录按实验编号;target 名与 run 脚本里调用的二进制名一致
add_executable(e1_dirty     01-dirty-watch/e1_dirty.cpp)
add_executable(e2_exit      02-exit-visibility/e2_exit.cpp)
add_executable(e3_bench     03-durability-bench/e3_bench.cpp)
add_executable(e4_metadata  04-fsync-fdatasync/e4_metadata.cpp)

foreach(t e1_dirty e2_exit e3_bench e4_metadata)
    target_compile_options(${t} PRIVATE ${COMMON_FLAGS})
endforeach()
