cmake_minimum_required(VERSION 3.20)
project(chapter10_06_critical_section CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 嵌入式友好选项
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 为每个示例创建可执行文件
add_executable(01_critical_section_basics 01_critical_section_basics.cpp)
add_executable(02_raii_critical_section 02_raii_critical_section.cpp)
add_executable(03_nested_locking 03_nested_locking.cpp)
add_executable(04_priority_inversion 04_priority_inversion.cpp)
add_executable(05_deadlock_prevention 05_deadlock_prevention.cpp)
