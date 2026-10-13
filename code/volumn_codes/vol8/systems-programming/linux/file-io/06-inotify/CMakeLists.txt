cmake_minimum_required(VERSION 3.14)
project(inotify-experiments LANGUAGES CXX)

# 这些实验依赖 Linux 专属接口(inotify / epoll / fork / timerfd),别在别处构建
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "本目录实验仅支持 Linux(WSL2 可),请在 Linux 环境下构建")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COMMON_FLAGS -Wall -Wextra -Wpedantic -O2 -g)

# 目录按实验编号;数据路径烧死在各源码开头(~/l06_scratch/eN),复跑前先 mkdir
add_executable(e1_panorama  01-event-panorama/event_panorama.cpp)
add_executable(e2_dirfile   02-dir-vs-file/dir_vs_file.cpp)
add_executable(e3_recursive 03-recursive-watch/recursive_watch.cpp)
add_executable(e4_queue     04-queue-merge/queue_merge.cpp)
add_executable(e5_epoll     05-epoll-loop/epoll_loop.cpp)
add_executable(e6_move      06-cross-dir-move/cross_dir_move.cpp)

foreach(t e1_panorama e2_dirfile e3_recursive e4_queue e5_epoll e6_move)
    target_compile_options(${t} PRIVATE ${COMMON_FLAGS})
    target_include_directories(${t} PRIVATE common)
endforeach()
