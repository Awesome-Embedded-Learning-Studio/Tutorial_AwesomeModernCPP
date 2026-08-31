cmake_minimum_required(VERSION 3.20)
project(ch02_mutex_condition_sync VERSION 0.0.1 LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(01_mutex_and_guards 01_mutex_and_guards.cpp)
target_link_libraries(01_mutex_and_guards PRIVATE Threads::Threads)

add_executable(02_deadlock_strategies 02_deadlock_strategies.cpp)
target_link_libraries(02_deadlock_strategies PRIVATE Threads::Threads)

add_executable(03_condition_variable 03_condition_variable.cpp)
target_link_libraries(03_condition_variable PRIVATE Threads::Threads)

add_executable(04_shared_mutex 04_shared_mutex.cpp)
target_link_libraries(04_shared_mutex PRIVATE Threads::Threads)

add_executable(05_sync_primitives 05_sync_primitives.cpp)
target_link_libraries(05_sync_primitives PRIVATE Threads::Threads)
