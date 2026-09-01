cmake_minimum_required(VERSION 3.20)
project(Vol1Ch05 VERSION 0.0.1 LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ch05/01-c-arrays.md
add_executable(c-arrays-decay c-arrays-decay.cpp)
add_executable(c-arrays-demo c-arrays-demo.cpp)

# ch05/02-std-array.md
add_executable(std-array-basics std-array-basics.cpp)
add_executable(std-array-access std-array-access.cpp)
add_executable(std-array-value-semantics std-array-value-semantics.cpp)
add_executable(std-array-ops std-array-ops.cpp)
add_executable(std-array-practice std-array-practice.cpp)

# ch05/03-std-string.md
add_executable(string-construct string-construct.cpp)
add_executable(string-demo string-demo.cpp)
