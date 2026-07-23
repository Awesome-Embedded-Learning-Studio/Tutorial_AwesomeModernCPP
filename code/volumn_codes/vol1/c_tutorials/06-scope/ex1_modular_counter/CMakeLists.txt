cmake_minimum_required(VERSION 3.20)
project(lesson_06_ex1_modular_counter C)

# 练习 1:模块化计数器 —— 多文件工程(main.c + counter.h + counter.c)。
# counter 内部用 static 变量维护计数,外部无法直接访问。
add_executable(counter main.c counter.c)
