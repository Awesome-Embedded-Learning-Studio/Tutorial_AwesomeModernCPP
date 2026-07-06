cmake_minimum_required(VERSION 3.20)
project(vol6_ch07_compiler_and_size LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 07-01 -O 级别 + optimization blockers(别名单文件多编几遍对比,见 README)
add_executable(opt_levels_blockers opt_levels_blockers.cpp)
target_compile_options(opt_levels_blockers PRIVATE -O2 -Wall -Wextra -Wpedantic)

# 07-02 LTO 跨 TU(lto_main + lto_helper)。开 LTO 版:CMake 用 INTERPROCEDURAL_OPTIMIZATION,
#   或直接命令行 g++ -O2 -flto lto_main.cpp lto_helper.cpp -o lto_lto(README 有对照脚本)
add_executable(lto_nolto lto_main.cpp lto_helper.cpp)
target_compile_options(lto_nolto PRIVATE -O2 -Wall -Wextra -Wpedantic)

# 07-04 体积优化(对照 -Os/-Oz/--gc-sections 见 README 脚本)
add_executable(size_demo size_demo.cpp)
target_compile_options(size_demo PRIVATE -O2 -ffunction-sections -fdata-sections -Wall -Wextra -Wpedantic)
target_link_options(size_demo PRIVATE -Wl,--gc-sections)
