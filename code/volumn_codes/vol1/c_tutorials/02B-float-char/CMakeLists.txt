cmake_minimum_required(VERSION 3.20)
project(lesson_02B_float_char C)

# 练习 1:浮点精度侦探 —— epsilon 比较函数(原文只有函数体,补了 main 演示)
add_executable(ex1_float_epsilon ex1_float_epsilon.c)

# 练习 2:隐式转换陷阱 —— 参考答案仅给出修复后的 if 片段(非完整实现),跳过抽取。

# 练习 3:const 实战 —— count_char 函数(原文只有函数体,补了 stddef.h 和 main 演示)
add_executable(ex3_count_char ex3_count_char.c)
