cmake_minimum_required(VERSION 3.20)
project(lesson_06_ex2_multi_file_visibility C)

# 练习 2:多文件符号可见性 —— 五个文件(main.c + a.h/a.c + b.h/b.c)。
# a.c 定义外部链接的 kSharedValue 和内部链接的 helper_a;
# b.c 也定义内部链接的 helper_a(同名不冲突),通过 extern 访问 kSharedValue。
add_executable(multi_file main.c a.c b.c)
