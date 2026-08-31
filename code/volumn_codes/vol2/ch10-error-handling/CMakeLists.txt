cmake_minimum_required(VERSION 3.20)
project(ch10_error_handling VERSION 0.0.1 LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(expected_parse_int expected_parse_int.cpp)
add_executable(expected_and_then expected_and_then.cpp)
add_executable(expected_or_else expected_or_else.cpp)
add_executable(expected_config_pipeline expected_config_pipeline.cpp)
add_executable(expected_address_parser expected_address_parser.cpp)
add_executable(optional_monadic_chain optional_monadic_chain.cpp)
add_executable(optional_config_parser optional_config_parser.cpp)
add_executable(error_aggregation error_aggregation.cpp)
