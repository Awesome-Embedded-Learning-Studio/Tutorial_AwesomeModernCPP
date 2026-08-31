cmake_minimum_required(VERSION 3.20)
project(lesson_03B_bitwise C)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 练习 1:位操作工具集 —— 4 个位操作函数(原文只有函数体,补了 stdint.h/stdio.h 和 main 演示)
add_executable(ex1_bit_ops ex1_bit_ops.c)

# 练习 2:安全的移位 —— safe_shift_left 函数(原文已有 stdint.h,补了 stdio.h 和 main 演示)
add_executable(ex2_safe_shift_left ex2_safe_shift_left.c)

# 练习 3:表达式分析 —— 参考答案是纯文字分析(无代码实现),跳过抽取。
