cmake_minimum_required(VERSION 3.20)
project(chapter06_03_shared_ptr_embedded_considerations CXX)

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

# 链接线程库（用于 atomic_overhead.cpp）
find_package(Threads REQUIRED)

# 为每个示例创建可执行文件
add_executable(basic_usage basic_usage.cpp)
add_executable(circular_reference circular_reference.cpp)
add_executable(atomic_overhead atomic_overhead.cpp)

# 链接线程库
target_link_libraries(atomic_overhead Threads::Threads)
