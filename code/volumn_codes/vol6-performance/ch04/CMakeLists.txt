cmake_minimum_required(VERSION 3.20)
project(vol6_ch04_tuning_by_bottleneck LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 04-01 后端内存:AoS vs SoA + 对齐/padding
add_executable(backend_memory backend_memory.cpp)
target_compile_options(backend_memory PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/O2;/W4>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2;-Wall;-Wextra;-Wpedantic>")

# 04-02 循环与计算:code motion / 消除内存引用(多累加器见 ch02/pipeline_branch_ilp)
add_executable(loop_opt loop_opt.cpp)
target_compile_options(loop_opt PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/O2;/W4>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2;-Wall;-Wextra;-Wpedantic>")

# 04-03 数据类型与算术:除法瓶颈 / 整数 vs 浮点 / switch 跳转表
add_executable(arithmetic_cost arithmetic_cost.cpp)
target_compile_options(arithmetic_cost PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/O2;/W4>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2;-Wall;-Wextra;-Wpedantic>")

# 04-04 / 06-01 虚函数与去虚拟化
add_executable(virtual_devirt virtual_devirt.cpp)
target_compile_options(virtual_devirt PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/O2;/W4>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2;-Wall;-Wextra;-Wpedantic>")

# 04-05 SIMD:标量 vs 手写 AVX2。⚠️ AVX2+FMA 必须显式开(-mavx2 -mfma),
#   且 FP 归约标量版默认不会被自动向量化(结合律),详见文章。
add_executable(simd_avx2 simd.cpp)
target_compile_options(simd_avx2 PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/O2;/arch:AVX2;/W4>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O3;-mavx2;-mfma;-Wall;-Wextra;-Wpedantic>")
# 对照:不开 AVX2 的标量基线
add_executable(simd_scalar simd.cpp)
target_compile_options(simd_scalar PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/O2;/W4>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2;-Wall;-Wextra;-Wpedantic>")

# 04-06 分支:branchless / predication
add_executable(branchless branchless.cpp)
target_compile_options(branchless PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/O2;/W4>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2;-Wall;-Wextra;-Wpedantic>")

# 04-07 / 07-02 PGO 演示。⚠️ 这个程序的完整 PGO 流程是三阶段(见 README.pgo.sh),
#   直接 cmake build 出来的是「纯 -O2 无 PGO」基线版,用于对照。
add_executable(pgo_demo pgo_demo.cpp)
target_compile_options(pgo_demo PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/O2;/W4>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2;-Wall;-Wextra;-Wpedantic>")
