cmake_minimum_required(VERSION 3.14)
project(vm-apis-experiments LANGUAGES CXX)

# 这些实验依赖 Linux 专属接口(procfs / fork / sigaction / mincore / mlock),别在别处构建
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "本目录实验仅支持 Linux(WSL2 可),请在 Linux 环境下构建")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# common/ 提供 article.hpp(unique_fd/sys_call/errno_code)与 sigout.hpp(write_all/write_hex)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/common)

# -O2 不只是性能开关:懒解析/常量折叠那两个坑,在 -O2 下才以存档里的形态现形
set(COMMON_FLAGS -Wall -Wextra -O2 -g)

add_executable(mprotect_deep     01-mprotect-deep/e1.cpp)
add_executable(madvise_family    02-madvise/e2.cpp)
add_executable(mlock_limits      03-mlock/e3.cpp)
add_executable(guarded_buffer    04-guarded-buffer/e4.cpp)
add_executable(mincore_bitmap    05-mincore/e5.cpp)
add_executable(process_vm_readv  06-process-vm-readv/e6.cpp)
