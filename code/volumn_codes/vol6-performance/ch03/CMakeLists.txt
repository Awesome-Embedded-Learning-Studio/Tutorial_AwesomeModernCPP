cmake_minimum_required(VERSION 3.20)
project(vol6_ch03_attribution_methodology LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ch03-01 Roofline:解析地算几个内核的算术强度,标注带宽/算力受限。
# 纯算术,无需 perf / 硬件计数器,任何机器都能跑。
add_executable(roofline roofline.cpp)
target_compile_options(roofline PRIVATE -O2 -Wall -Wextra -Wpedantic)

# profile-commands.sh 是命令速查参考(需 perf/toplev/FlameGraph,本机 WSL2 无),
# 不构建目标。详见脚本内注释 + ch03-02/03。
