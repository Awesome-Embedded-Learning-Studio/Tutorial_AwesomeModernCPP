cmake_minimum_required(VERSION 3.14)
project(lab0-mini-reactor LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Catch2 v3(FetchContent 拉取;首次配置需联网)
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.0)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

# ---------- 普通版:reactor 库(参考实现)+ 测试 ----------
add_library(reactor STATIC src/reactor.cpp)
target_include_directories(reactor PUBLIC include)
target_compile_options(reactor PUBLIC
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-g>")

add_executable(lab0_tests tests/lab0_tests.cpp)
target_link_libraries(lab0_tests PRIVATE reactor Catch2::Catch2WithMain)
target_compile_options(lab0_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-g;-pthread>")
target_link_options(lab0_tests PRIVATE -pthread)

# ---------- TSan 版:同样的源码,编译期开 -fsanitize=thread ----------
# reactor 库也单独编一份 tsan 版,否则它内部的并发(如 stop_ 原子)在 TSan 下不可见
add_library(reactor_tsan STATIC src/reactor.cpp)
target_include_directories(reactor_tsan PUBLIC include)
target_compile_options(reactor_tsan PUBLIC -Wall -Wextra -Wpedantic -g -fsanitize=thread -pthread)
target_link_options(reactor_tsan PUBLIC -fsanitize=thread)

add_executable(lab0_tests_tsan tests/lab0_tests.cpp)
target_link_libraries(lab0_tests_tsan PRIVATE reactor_tsan Catch2::Catch2WithMain)
target_compile_options(lab0_tests_tsan PRIVATE -Wall -Wextra -Wpedantic -g -fsanitize=thread -pthread)
target_link_options(lab0_tests_tsan PRIVATE -fsanitize=thread)

# 便捷:make check = 跑普通测试
add_custom_target(check COMMAND $<TARGET_FILE:lab0_tests>)
