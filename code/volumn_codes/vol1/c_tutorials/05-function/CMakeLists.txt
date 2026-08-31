cmake_minimum_required(VERSION 3.20)
project(lesson_05_function C)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 练习 1:可变参数日志函数 —— log_message(完整函数体)。
add_executable(ex1_log_message ex1_log_message.c)

# 练习 2:二分查找 —— binary_search_recursive + binary_search_iterative(完整函数体)。
add_executable(ex2_binary_search ex2_binary_search.c)

# 练习 3:多返回值实战 —— find_min_max(完整函数体)。
add_executable(ex3_find_min_max ex3_find_min_max.c)
