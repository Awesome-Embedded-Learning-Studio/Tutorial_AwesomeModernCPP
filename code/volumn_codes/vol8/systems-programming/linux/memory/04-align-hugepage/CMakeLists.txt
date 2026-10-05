cmake_minimum_required(VERSION 3.14)
project(align-hugepage-experiments LANGUAGES CXX)

# 这些实验依赖 Linux 专属接口(madvise/MADV_HUGEPAGE/prctl//proc),别在别处构建
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "本目录实验仅支持 Linux(WSL2 可),请在 Linux 环境下构建")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COMMON_FLAGS -Wall -Wextra -Wpedantic -O2 -g)
find_package(Threads REQUIRED)

# 目录按实验编号;e2_avx 的 AVX 指令靠函数级 target("avx2") 属性,不加全局 -mavx2
add_executable(e1_aligned_alloc  01-aligned-alloc/e1_aligned_alloc.cpp)
add_executable(e2_avx            02-why-align/e2_avx.cpp)
add_executable(e2_false_sharing  02-why-align/e2_false_sharing.cpp)
add_executable(e3_thp            03-thp/e3_thp.cpp)
add_executable(e3_thp_probe      03-thp/e3_thp_probe.cpp)
add_executable(e4_hugetlb        04-hugetlb/e4_hugetlb.cpp)
add_executable(e5_overcommit     05-overcommit/e5_overcommit.cpp)
add_executable(e6_oom            06-oom/e6_oom.cpp)

foreach(t e1_aligned_alloc e2_avx e3_thp e3_thp_probe e4_hugetlb e5_overcommit e6_oom e2_false_sharing)
    target_compile_options(${t} PRIVATE ${COMMON_FLAGS})
endforeach()
target_link_libraries(e2_false_sharing PRIVATE Threads::Threads)
