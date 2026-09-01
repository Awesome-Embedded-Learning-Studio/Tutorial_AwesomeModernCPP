cmake_minimum_required(VERSION 3.14)
project(modern-socket-echo LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 23)            # std::expected / std::print 需要 C++23
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COMMON_FLAGS -Wall -Wextra -Wpedantic -g)

# 文章 01:Modern C++(RAII + std::expected)echo server + client + C10K 探测
# server 用了 std::thread,需要 -pthread(编译期 + 链接期都加)
add_executable(echo_server echo_server.cpp)
target_compile_options(echo_server PRIVATE ${COMMON_FLAGS} -pthread)
target_link_options(echo_server PRIVATE -pthread)

add_executable(echo_client echo_client.cpp)
target_compile_options(echo_client PRIVATE ${COMMON_FLAGS})

add_executable(hold_clients hold_clients.cpp)
target_compile_options(hold_clients PRIVATE ${COMMON_FLAGS})
