cmake_minimum_required(VERSION 3.20)
project(ch02_mutex_condition_sync VERSION 0.0.1 LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(02_thread_local 02_thread_local.cpp)
target_link_libraries(02_thread_local PRIVATE Threads::Threads)

add_executable(01_mutex_and_raii_guards 01_mutex_and_raii_guards.cpp)
target_link_libraries(01_mutex_and_raii_guards PRIVATE Threads::Threads)

add_executable(03_deadlock_and_gdb 03_deadlock_and_gdb.cpp)
target_link_libraries(03_deadlock_and_gdb PRIVATE Threads::Threads)

add_executable(05_sync_primitives_toolkit 05_sync_primitives_toolkit.cpp)
target_link_libraries(05_sync_primitives_toolkit PRIVATE Threads::Threads)

# ch02/04《condition_variable 与阻塞队列》(正文配套,补注册)
add_executable(04_condition_variable_and_bounded_queue 04_condition_variable_and_bounded_queue.cpp)
target_link_libraries(04_condition_variable_and_bounded_queue PRIVATE Threads::Threads)

# ch02/02《thread_local》正文的两个独立实验(输出/退出码与正文输出块逐字对拍)
add_executable(02_thread_local_exit 02_thread_local_exit.cpp)
target_link_libraries(02_thread_local_exit PRIVATE Threads::Threads)

add_executable(02_thread_local_throw 02_thread_local_throw.cpp)
target_link_libraries(02_thread_local_throw PRIVATE Threads::Threads)

# ch02/03《死锁与现场诊断》正文的独立现场程序(35 行,行号与文中 gdb 会话锚定,
# 说明注释放在文件尾部,别在前头加行)
add_executable(deadlock deadlock.cpp)
target_link_libraries(deadlock PRIVATE Threads::Threads)
