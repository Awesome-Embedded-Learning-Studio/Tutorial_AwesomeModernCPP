cmake_minimum_required(VERSION 3.20)
project(lesson_02B_float_char C)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 练习 1:浮点精度侦探 —— epsilon 比较函数(原文只有函数体,补了 main 演示)
add_executable(ex1_float_epsilon ex1_float_epsilon.c)

# 练习 2:隐式转换陷阱 —— 参考答案仅给出修复后的 if 片段(非完整实现),跳过抽取。

# 练习 3:const 实战 —— count_char 函数(原文只有函数体,补了 stddef.h 和 main 演示)
add_executable(ex3_count_char ex3_count_char.c)
