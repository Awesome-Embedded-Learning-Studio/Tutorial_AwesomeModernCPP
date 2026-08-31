cmake_minimum_required(VERSION 3.20)
project(target_demo LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# fmt:仅在本工程内部使用的极简"第三方库",真实工程会换成 find_package(fmt REQUIRED)
add_library(fmt STATIC fmt/fmt.cpp)
target_include_directories(fmt PUBLIC fmt)

# mylib:对外暴露的库,公开头文件 include/mylib/mylib.h 用了 std::string
add_library(mylib STATIC src/mylib.cpp)
target_include_directories(mylib PUBLIC include)
# fmt 在这里写成 PRIVATE —— mylib.cpp 内部要用,但 mylib.h 完全不暴露 fmt
target_link_libraries(mylib PRIVATE fmt)

# app:下游可执行,只链接 mylib,对 fmt 一无所知
add_executable(app main.cpp)
target_link_libraries(app PRIVATE mylib)
