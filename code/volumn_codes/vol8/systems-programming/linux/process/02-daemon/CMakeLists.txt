cmake_minimum_required(VERSION 3.14)
project(daemon-session-experiments LANGUAGES CXX)

# 这些实验依赖 Linux 专属接口(fork/setsid/信号/pty/rlimit),别在别处构建
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "本目录实验仅支持 Linux(WSL2 可),请在 Linux 环境下构建")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COMMON_FLAGS -Wall -Wextra -Wpedantic -O2 -g)

# 目录按实验编号;target 名与 README 复现命令里的二进制名一致
add_executable(e1_sessions     01-session-pgrp/e1_sessions.cpp)
add_executable(e1_pgrp_sighup  01-session-pgrp/e1_pgrp_sighup.cpp)
add_executable(e2_daemon_steps 02-daemonize/e2_daemon_steps.cpp)
add_executable(e2b_daemon_libc 02-daemonize/e2b_daemon_libc.cpp)
add_executable(e3_tty_hangup   03-terminal/e3_tty_hangup.cpp)
add_executable(e4_nofile       04-rlimit/e4_nofile.cpp)
add_executable(e4_cpu          04-rlimit/e4_cpu.cpp)
add_executable(e4_stack        04-rlimit/e4_stack.cpp)
add_executable(e4_hardlimit    04-rlimit/e4_hardlimit.cpp)
add_executable(e5_environ      05-environ/e5_environ.cpp)
add_executable(e5_exec         05-environ/e5_exec.cpp)
add_executable(e6_proc         06-procfs/e6_proc.cpp)

foreach(t e1_sessions e1_pgrp_sighup e2_daemon_steps e2b_daemon_libc e3_tty_hangup
         e4_nofile e4_cpu e4_stack e4_hardlimit e5_environ e5_exec e6_proc)
    target_compile_options(${t} PRIVATE ${COMMON_FLAGS})
endforeach()

target_link_libraries(e3_tty_hangup PRIVATE util)  # openpty
