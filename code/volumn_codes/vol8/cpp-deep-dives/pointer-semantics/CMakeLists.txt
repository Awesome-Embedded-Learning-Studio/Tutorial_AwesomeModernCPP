cmake_minimum_required(VERSION 3.14)
project(pointer-semantics-demo LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC 用 /W4;-g 是 GCC/Clang 调试信息惯例,MSVC 多配置由 /Zi 自带
set(COMMON_FLAGS
    "$<$<CXX_COMPILER_ID:MSVC>:/W4>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-g>")

# 文章 01：Borrowed<T> 和 ObserverPtr<T>
add_executable(test_borrowed_observer test_borrowed_observer.cpp)
target_compile_options(test_borrowed_observer PRIVATE ${COMMON_FLAGS})

# 文章 02：UnsafeWeakPtr UB 演示
add_executable(test_unsafe_ub test_unsafe_weak_ptr_ub.cpp)
target_compile_options(test_unsafe_ub PRIVATE ${COMMON_FLAGS})

# 文章 02：UnsafeWeakPtr UB 演示（ASan 版本）
# ASan/UBSan 组合是 Linux GCC/Clang 工具链特性
# (mingw 的 GCC 无 ASan runtime),Windows 全系不构建此 target
if(NOT WIN32)
    add_executable(test_unsafe_ub_asan test_unsafe_weak_ptr_ub.cpp)
    target_compile_options(test_unsafe_ub_asan PRIVATE ${COMMON_FLAGS} -fsanitize=address,undefined)
    target_link_options(test_unsafe_ub_asan PRIVATE -fsanitize=address,undefined)
endif()

# 文章 03：SimpleWeakPtr 安全失效检测
add_executable(test_simple_wp test_simple_weak_ptr.cpp)
target_compile_options(test_simple_wp PRIVATE ${COMMON_FLAGS})

# 文章 04：Chrome-like WeakPtr 生命周期验证
add_executable(test_lifetime test_lifetime_verification.cpp)
target_compile_options(test_lifetime PRIVATE ${COMMON_FLAGS})
