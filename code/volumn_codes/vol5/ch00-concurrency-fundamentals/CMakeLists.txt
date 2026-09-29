cmake_minimum_required(VERSION 3.20)
project(ch00_concurrency_fundamentals VERSION 0.0.1 LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(false_sharing_bench false_sharing_bench.cpp)
target_link_libraries(false_sharing_bench PRIVATE Threads::Threads)

# ch00/02 数据竞争与 ThreadSanitizer 第一课的四个示例
add_executable(data_race 01_data_race.cpp)
target_link_libraries(data_race PRIVATE Threads::Threads)

add_executable(data_race_mutex 02_data_race_mutex.cpp)
target_link_libraries(data_race_mutex PRIVATE Threads::Threads)

add_executable(check_then_act 03_check_then_act.cpp)
target_link_libraries(check_then_act PRIVATE Threads::Threads)

add_executable(deadlock_reorder 04_deadlock_reorder.cpp)
target_link_libraries(deadlock_reorder PRIVATE Threads::Threads)
