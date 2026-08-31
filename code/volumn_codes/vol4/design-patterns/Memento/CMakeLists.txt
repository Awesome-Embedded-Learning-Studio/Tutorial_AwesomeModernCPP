cmake_minimum_required(VERSION 3.10)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(Memento VERSION 0.1.0 LANGUAGES C CXX)


# mingw 的 g++ 驱动不自动链 libstdc++exp(<print> 的 terminal 后端符号库);
# Linux GCC 主库已含,MSVC 无此概念。不链则链接期报
# undefined reference to std::__open_terminal。须在 add_executable 之前。
if(MINGW)
    link_libraries(stdc++exp)
endif()

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_executable(TextEditor TextEditor.h TextEditorMain.cpp)
