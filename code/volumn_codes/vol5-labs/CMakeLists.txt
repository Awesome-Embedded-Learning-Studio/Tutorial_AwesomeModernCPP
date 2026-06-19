# 卷五练习手册统一工程
#
# 本顶层只编译 examples/ 下的「参考实现」(作者验证用;CI 跑这些保证参考实现始终可构建、测试全绿)。
# templates/ 是空实现骨架,给初学者拷贝去做练习,不在此编译。
#
# 每个 Lab 工程都是 standalone,可独立构建(IDE/clangd 友好):
#   cd examples/lab0_thread_lifecycle   # 或 templates/lab0_thread_lifecycle
#   cmake -B build -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build
#
# 一键构建全部参考实现:
#   cmake -B build -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build
#   ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.20)

project(Vol5Labs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 顶层拉一次 Catch2,被 add_subdirectory 的 example 复用(FetchContent 全局缓存)。
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)

enable_testing()

# 编译参考实现做验证。templates/ 不编译(空实现,给初学者拷贝)。
# 注意:若 example 的参考实现尚未补全(include/lab0/ 还是空声明),
#       此处会停在链接阶段报 undefined reference —— 那是预期,提示参考实现待写。
add_subdirectory(examples/lab0_thread_lifecycle)
# 将来新增 Lab 在此追加:
# add_subdirectory(examples/lab1_bounded_queue)   # 待 example 参考实现完成取消注释(空声明会停在链接 undefined reference)
# add_subdirectory(examples/lab1_xxx)
