cmake_minimum_required(VERSION 3.20)
project(chapter11_auto_decltype CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 嵌入式友好选项
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
# 警告旗标按编译器分派:MSVC 用 /W4,GCC/Clang 用 -Wall -Wextra(Windows 免 WSL 直接构建)
add_compile_options(
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra>"
)

# 为每个示例创建可执行文件
add_executable(basic_auto basic_auto.cpp)
add_executable(auto_with_references auto_with_references.cpp)
add_executable(decltype_basics decltype_basics.cpp)
add_executable(decltype_auto_trailing_return decltype_auto_trailing_return.cpp)
add_executable(embedded_hal_types embedded_hal_types.cpp)
add_executable(auto_pitfalls auto_pitfalls.cpp)

# 可选：禁用异常和RTTI（嵌入式常见）
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")
