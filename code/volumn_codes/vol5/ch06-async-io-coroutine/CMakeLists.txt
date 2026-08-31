cmake_minimum_required(VERSION 3.20)
project(ch06_async_io_coroutine VERSION 0.0.1 LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(01_coroutine_basics 01_coroutine_basics.cpp)
target_link_libraries(01_coroutine_basics PRIVATE Threads::Threads)

add_executable(02_awaitable_task 02_awaitable_task.cpp)
target_link_libraries(02_awaitable_task PRIVATE Threads::Threads)

add_executable(03_timer_scheduler 03_timer_scheduler.cpp)
target_link_libraries(03_timer_scheduler PRIVATE Threads::Threads)
