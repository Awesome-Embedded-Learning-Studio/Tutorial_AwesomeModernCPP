cmake_minimum_required(VERSION 3.20)
project(lesson_03A_operators C)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 练习 1:整数除法预测 —— 参考答案仅给出预测值(纯文字),无完整实现,跳过抽取。

# 练习 2:短路求值实战 —— find_first_above 函数(原文只有函数体,补了 stdio.h 和 main 演示)
add_executable(ex2_find_first_above ex2_find_first_above.c)
