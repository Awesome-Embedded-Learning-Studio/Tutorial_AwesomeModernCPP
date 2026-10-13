cmake_minimum_required(VERSION 3.14)
project(mmap-experiments LANGUAGES CXX)

# 这些实验依赖 Linux 专属接口(procfs / fork / sigaction / mmap),别在别处构建
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "本目录实验仅支持 Linux(WSL2 可),请在 Linux 环境下构建")
endif()

set(CMAKE_CXX_STANDARD 23)            # std::print 需要 C++23(实测 g++ 16.2.1)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# common/ 提供 article.hpp(unique_fd/sys_call/errno_code)与 sigout.hpp;
# 多数源码用裸 #include "article.hpp",bench.cpp 用 ../common/ 相对路径,统一 -I 覆盖
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/common)

# -O2 不是可选项:e1.cpp 的 volatile sig_atomic_t 重排故事,只有在 -O2 下才现形
set(COMMON_FLAGS -Wall -Wextra -Wpedantic -O2 -g)

# 目录按文章小节顺序编号,对照表见 README.md;target 名即实验名
add_executable(fault_cost     01-fault-cost/fault_cost.cpp)
add_executable(proc_maps      02-proc-maps/e3.cpp)
add_executable(sigbus         03-sigbus/sigbus.cpp)
add_executable(mprotect_guard 04-mprotect-guard/e1.cpp)
add_executable(mprotect_file  05-mprotect-file/e2.cpp)
add_executable(shared_vis     06-shared-vis/e4.cpp)
add_executable(dirty_msync    07-dirty-msync/e5.cpp)
add_executable(cow            08-cow/cow.cpp)
add_executable(bench          09-bench/bench.cpp)

foreach(t fault_cost proc_maps sigbus mprotect_guard mprotect_file shared_vis dirty_msync cow bench)
    target_compile_options(${t} PRIVATE ${COMMON_FLAGS})
endforeach()
