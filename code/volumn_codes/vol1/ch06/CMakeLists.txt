cmake_minimum_required(VERSION 3.20)
project(Vol1Ch06 VERSION 0.0.1 LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(point point.cpp)
add_executable(constructors constructors.cpp)
add_executable(ctor_dtor_order ctor_dtor_order.cpp)
add_executable(raii_demo raii_demo.cpp)
add_executable(static_demo static_demo.cpp)
add_executable(friend_demo friend_demo.cpp)
add_executable(cascading_demo cascading_demo.cpp)
