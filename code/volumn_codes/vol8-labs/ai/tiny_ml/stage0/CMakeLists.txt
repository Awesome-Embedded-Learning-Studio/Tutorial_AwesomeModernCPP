cmake_minimum_required(VERSION 3.20)

project(tamcpp_mlinfra LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 不调这个，CMake 不生成 CTestTestfile.cmake，catch_discover_tests 注册的
# add_test 全悬空，ctest 永远 No tests found。
enable_testing()

message(STATUS "Ready to fetch the necessary test framework")

include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.0
)

FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

# 顶层只把 Catch2 的 extras 加进了 MODULE_PATH，要用 catch_discover_tests
# 还得自己 include(Catch) 把命令加载进来。
include(Catch)

# 跨编译器的 warning 选项封进一个函数，后续每个 target 一行调用就行，
# 不用在 CMakeLists.txt 里反复抄 generator-expression。
# 比较坑的点：这个 Lab 要同时吃 MSVC 和 GCC/Clang，两边选项根本不通用，
# 只能用 genex 按编译器分流——还得是跑到 Windows 下编译一轮才吃到这个亏！
function(tamcpp_target_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive-;/Zi>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-g>
    )
endfunction()

add_executable(smoke_catch2 tests/smoke.cpp)
target_link_libraries(smoke_catch2 PRIVATE Catch2::Catch2WithMain)
tamcpp_target_warnings(smoke_catch2)
catch_discover_tests(smoke_catch2)
