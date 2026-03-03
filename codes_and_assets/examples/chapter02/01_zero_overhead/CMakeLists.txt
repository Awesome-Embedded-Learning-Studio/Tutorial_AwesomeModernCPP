cmake_minimum_required(VERSION 3.20)
project(chapter02_01_zero_overhead CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 嵌入式友好选项
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 为每个示例创建可执行文件
add_executable(gpio_example gpio_example.cpp)
add_executable(state_machine state_machine.cpp)
add_executable(raii_example raii_example.cpp)
add_executable(constexpr_example constexpr_example.cpp)
add_executable(inline_vs_macro inline_vs_macro.cpp)
add_executable(loop_unroll loop_unroll.cpp)
add_executable(strong_types strong_types.cpp)

# 如果需要禁用异常和 RTTI（嵌入式常见）
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")
