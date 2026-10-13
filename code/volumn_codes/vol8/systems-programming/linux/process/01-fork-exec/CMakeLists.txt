cmake_minimum_required(VERSION 3.14)
project(fork-exec-experiments LANGUAGES CXX)

# 这些实验依赖 Linux 专属接口(fork/clone/execve/waitpid/procfs),别在别处构建
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "本目录实验仅支持 Linux(WSL2 可),请在 Linux 环境下构建")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 目录按实验编号(E1..E6);target 名与 run_all.sh 里调用的二进制名一致
add_executable(e1a_fork_twice       e1-fork-ledger/e1a_fork_twice.cpp)
add_executable(e1b_cow              e1-fork-ledger/e1b_cow.cpp)
add_executable(e1c_fork_cost        e1-fork-ledger/e1c_fork_cost.cpp)
add_executable(e1d_shared_offset    e1-fork-ledger/e1d_shared_offset.cpp)
add_executable(e2a_exec_variants    e2-exec-family/e2a_exec_variants.cpp)
add_executable(e2b_exec_fd          e2-exec-family/e2b_exec_fd.cpp)
add_executable(e2c_exec_fail        e2-exec-family/e2c_exec_fail.cpp)
add_executable(e2d_strace_target    e2-exec-family/e2d_strace_target.cpp)
add_executable(e3a_zombie_lifecycle e3-zombie-reap/e3a_zombie_lifecycle.cpp)
add_executable(e3b_reap_strategies  e3-zombie-reap/e3b_reap_strategies.cpp)
add_executable(e3c_exit_status      e3-zombie-reap/e3c_exit_status.cpp)
add_executable(e4_orphan            e4-orphan/e4_orphan.cpp)
add_executable(e5a_spawn_compare    e5-posix-spawn/e5a_spawn_compare.cpp)
add_executable(e5b_spawn_actions    e5-posix-spawn/e5b_spawn_actions.cpp)
add_executable(e6_child_process     e6-raii-child/e6_child_process.cpp)

foreach(t
        e1a_fork_twice e1b_cow e1c_fork_cost e1d_shared_offset
        e2a_exec_variants e2b_exec_fd e2c_exec_fail e2d_strace_target
        e3a_zombie_lifecycle e3b_reap_strategies e3c_exit_status
        e4_orphan e5a_spawn_compare e5b_spawn_actions e6_child_process)
    target_compile_options(${t} PRIVATE -Wall -Wextra -O2)
endforeach()
