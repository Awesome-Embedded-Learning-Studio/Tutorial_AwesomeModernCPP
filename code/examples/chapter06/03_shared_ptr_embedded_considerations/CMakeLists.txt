cmake_minimum_required(VERSION 3.20)
project(chapter06_03_shared_ptr_embedded_considerations CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 嵌入式友好选项
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 链接线程库（用于 atomic_overhead.cpp）
find_package(Threads REQUIRED)

# 为每个示例创建可执行文件
add_executable(basic_usage basic_usage.cpp)
add_executable(circular_reference circular_reference.cpp)
add_executable(atomic_overhead atomic_overhead.cpp)

# 链接线程库
target_link_libraries(atomic_overhead Threads::Threads)
