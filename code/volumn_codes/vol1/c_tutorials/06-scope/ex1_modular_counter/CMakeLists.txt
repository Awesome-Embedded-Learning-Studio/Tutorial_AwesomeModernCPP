cmake_minimum_required(VERSION 3.20)
project(lesson_06_ex1_modular_counter C)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 练习 1:模块化计数器 —— 多文件工程(main.c + counter.h + counter.c)。
# counter 内部用 static 变量维护计数,外部无法直接访问。
add_executable(counter main.c counter.c)
