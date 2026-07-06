cmake_minimum_required(VERSION 3.20)
project(vol6_ch02_cpu_microarchitecture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 02-01 存储层次:memory mountain(吞吐 size×stride)+ 指针追逐延迟阶梯
add_executable(memory_mountain memory_mountain.cpp)
target_compile_options(memory_mountain PRIVATE -O2 -Wall -Wextra -Wpedantic)

# 02-02 缓存行与局部性:步长扫描定位 64B 断崖 + 行优先 vs 列优先 2D 遍历
add_executable(cacheline_locality cacheline_locality.cpp)
target_compile_options(cacheline_locality PRIVATE -O2 -Wall -Wextra -Wpedantic)

# 02-03 流水线/ILP/分支预测
# ⚠️ pipeline_branch_ilp 必须用下面的反优化 flag,否则:
#   - GCC 默认 -O2 会把 if 条件累加自动向量化 / 转 cmov → 分支 demo(4.2x)消失
#   - 单累加器 dot1 会被循环不变量提升整体消除 → ILP demo 失真
# 这组 flag 是文章论证成立的前提,别「优化」掉。
add_executable(pipeline_branch_ilp pipeline_branch_ilp.cpp)
target_compile_options(pipeline_branch_ilp PRIVATE
    -O2 -fno-tree-vectorize -fno-tree-slp-vectorize -fno-if-conversion
    -Wall -Wextra -Wpedantic)

# 02-04 TLB / huge page:同工作集 4KB 页 vs 2MB 大页(THP)指针追逐
add_executable(tlb_hugepage tlb_hugepage.cpp)
target_compile_options(tlb_hugepage PRIVATE -O2 -Wall -Wextra -Wpedantic)
