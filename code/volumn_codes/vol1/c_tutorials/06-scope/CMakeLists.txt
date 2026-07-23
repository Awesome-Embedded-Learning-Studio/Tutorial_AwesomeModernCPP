cmake_minimum_required(VERSION 3.20)
project(lesson_06_scope C)

# 练习 1:模块化计数器 —— 多文件工程(main.c + counter.h + counter.c),
# 放在子目录 ex1_modular_counter/(独立 CMake project,本顶层不 add_subdirectory)。

# 练习 2:多文件符号可见性 —— 五个文件(main.c + a.h/a.c + b.h/b.c),
# 放在子目录 ex2_multi_file_visibility/(独立 CMake project)。

# 练习 3:延迟初始化 —— get_config + Config typedef + main(单文件完整)。
add_executable(ex3_get_config ex3_get_config.c)
