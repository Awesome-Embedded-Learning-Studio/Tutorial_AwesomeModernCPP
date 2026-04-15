cmake_minimum_required(VERSION 3.20)
project(chapter11_auto_decltype CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 嵌入式友好选项
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 为每个示例创建可执行文件
add_executable(basic_auto basic_auto.cpp)
add_executable(auto_with_references auto_with_references.cpp)
add_executable(decltype_basics decltype_basics.cpp)
add_executable(decltype_auto_trailing_return decltype_auto_trailing_return.cpp)
add_executable(embedded_hal_types embedded_hal_types.cpp)
add_executable(auto_pitfalls auto_pitfalls.cpp)

# 可选：禁用异常和RTTI（嵌入式常见）
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")
