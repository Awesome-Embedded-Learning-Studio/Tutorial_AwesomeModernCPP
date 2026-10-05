cmake_minimum_required(VERSION 3.14)
project(shm-experiments LANGUAGES CXX)

# 这些实验依赖 Linux 专属接口(shm_open / fork / SCM_RIGHTS / memfd_create),别在别处构建
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "本目录实验仅支持 Linux(WSL2 可),请在 Linux 环境下构建")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# glibc >= 2.34 后 shm_open/sem_* 都在 libc,不需要单独链 rt
set(COMMON_FLAGS -Wall -Wextra -O2)

add_executable(e1_lifecycle 01-lifecycle/e1_lifecycle.cpp)
add_executable(e1_perm      01-lifecycle/e1_perm.cpp)
add_executable(e2_race      02-race/e2_race.cpp)
add_executable(e3_two_ways  03-two-ways/e3_two_ways.cpp)
add_executable(e4_spsc      04-spsc-queue/e4_spsc.cpp)
add_executable(e4_spsc_pinned 04-spsc-queue/e4_spsc_pinned.cpp)
add_executable(e5_ipc       05-ipc-baseline/e5_ipc.cpp)
add_executable(e6_memfd     06-memfd/e6_memfd.cpp)

foreach(t e1_lifecycle e1_perm e2_race e3_two_ways e4_spsc e4_spsc_pinned e5_ipc e6_memfd)
    target_compile_options(${t} PRIVATE ${COMMON_FLAGS})
endforeach()

# pthread 名字在 glibc 2.34+ 同样并入了 libc,但 -pthread 还带 _REENTRANT 语义,保留
foreach(t e2_race e3_two_ways e4_spsc e4_spsc_pinned e5_ipc)
    target_compile_options(${t} PRIVATE -pthread)
endforeach()
