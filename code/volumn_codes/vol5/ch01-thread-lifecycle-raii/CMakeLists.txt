cmake_minimum_required(VERSION 3.20)
project(ch01_thread_lifecycle_raii VERSION 0.0.1 LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(01_thread_creation 01_thread_creation.cpp)
target_link_libraries(01_thread_creation PRIVATE Threads::Threads)

add_executable(02_thread_arguments_lifetime 02_thread_arguments_lifetime.cpp)
target_link_libraries(02_thread_arguments_lifetime PRIVATE Threads::Threads)

# 本篇演示含 [=, this] / [=, *this] 捕获(旧文重写版),需要 C++20
add_executable(02_thread_arguments_and_lifetime 02_thread_arguments_and_lifetime.cpp)
target_link_libraries(02_thread_arguments_and_lifetime PRIVATE Threads::Threads)
set_target_properties(02_thread_arguments_and_lifetime PROPERTIES CXX_STANDARD 20)

add_executable(03_raii_thread_guard 03_raii_thread_guard.cpp)
target_link_libraries(03_raii_thread_guard PRIVATE Threads::Threads)

add_executable(04_thread_local_call_once 04_thread_local_call_once.cpp)
target_link_libraries(04_thread_local_call_once PRIVATE Threads::Threads)

# ch01/01《std::thread 基础》正文配套(正文口径 -std=c++20 编译验证)
add_executable(01_std_thread 01_std_thread.cpp)
target_link_libraries(01_std_thread PRIVATE Threads::Threads)
set_target_properties(01_std_thread PROPERTIES CXX_STANDARD 20)

# ch01/03《线程所有权与 jthread/stop_token》:jthread 与 <stop_token> 三件套需要 C++20
add_executable(03_thread_ownership_and_jthread 03_thread_ownership_and_jthread.cpp)
target_link_libraries(03_thread_ownership_and_jthread PRIVATE Threads::Threads)
set_target_properties(03_thread_ownership_and_jthread PROPERTIES CXX_STANDARD 20)
