cmake_minimum_required(VERSION 3.20)
project(lesson_06_ex2_multi_file_visibility C)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 练习 2:多文件符号可见性 —— 五个文件(main.c + a.h/a.c + b.h/b.c)。
# a.c 定义外部链接的 kSharedValue 和内部链接的 helper_a;
# b.c 也定义内部链接的 helper_a(同名不冲突),通过 extern 访问 kSharedValue。
add_executable(multi_file main.c a.c b.c)
