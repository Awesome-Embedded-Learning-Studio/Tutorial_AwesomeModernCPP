cmake_minimum_required(VERSION 3.31.0)

project(tamcpp_ministl LANGUAGES CXX VERSION 0.1.0)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 导出编译数据库,clangd 靠它才知道 -Iinclude 这些参数
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- 头文件库本体 ----------------------------------------------------------
# INTERFACE 目标:不产出 .a/.so,只把 include 路径和语言标准传给使用者
add_library(tamcpp_ministl INTERFACE)
target_include_directories(tamcpp_ministl INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(tamcpp_ministl INTERFACE cxx_std_23)

# ---- 公共配置 --------------------------------------------------------------
# 本项目的默认姿态(GCC 系):警告全开 + ASan/UBSan 双探测器。
# 哪天要裸跑性能,用 -DTAMCPP_MINISTL_SANITIZE=OFF 关掉。
option(TAMCPP_MINISTL_SANITIZE "Build examples and tests with ASan+UBSan" ON)

# 一个 .cpp 拉一个可执行:统一挂库、挂警告、挂探测器
function(tamcpp_add_exec name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE tamcpp_ministl)
    target_compile_options(${name} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra>")

    # ASan+UBSan 是 Linux GCC/Clang 工具链特性(mingw 无 ASan runtime,
    # MSVC 的 /fsanitize=address 仅 ASan 且配接管运行时)——Windows 全系跳过
    if(TAMCPP_MINISTL_SANITIZE AND NOT WIN32)
        target_compile_options(${name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${name} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

enable_testing()

add_subdirectory(example)
add_subdirectory(tests)
