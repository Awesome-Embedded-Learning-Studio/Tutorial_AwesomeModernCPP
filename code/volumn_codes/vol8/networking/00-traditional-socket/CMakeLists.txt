cmake_minimum_required(VERSION 3.14)
project(traditional-socket-echo LANGUAGES C)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COMMON_FLAGS -Wall -Wextra -Wpedantic -g)

# 文章 00:传统 C 风格 echo server/client(BSD socket 五步)
add_executable(echo_server classic_server.c)
target_compile_options(echo_server PRIVATE ${COMMON_FLAGS})

add_executable(echo_client classic_client.c)
target_compile_options(echo_client PRIVATE ${COMMON_FLAGS})
