cmake_minimum_required(VERSION 3.20)

project(tamcpp_mlinfra LANGUAGES CXX)


# mingw 的 g++ 驱动不自动链 libstdc++exp(<print> 的 terminal 后端符号库);
# Linux GCC 主库已含,MSVC 无此概念。不链则链接期报
# undefined reference to std::__open_terminal。须在 add_executable 之前。
if(MINGW)
    link_libraries(stdc++exp)
endif()

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

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

add_library(TAMCPP_TinyML INTERFACE include/tinyml/tensor.hpp include/tinyml/dense.hpp include/tinyml/debug.hpp)
target_include_directories(TAMCPP_TinyML INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 跨编译器的 warning 选项封进一个函数，后续每个 target 一行调用就行，
# 不用在每个 CMakeLists.txt 里反复抄一遍 generator-expression。
# 比较坑的点：这个 Lab 要同时吃 MSVC 和 GCC/Clang，两边选项根本不通用，
# 只能用 genex 按编译器分流——还得是跑到 Windows 下编译一轮才吃到这个亏！
function(tamcpp_target_warnings target)
    target_compile_options(${target} PRIVATE
        "$<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive-;/Zi>"
        "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-g>"
    )
endfunction()

add_subdirectory(tests)
