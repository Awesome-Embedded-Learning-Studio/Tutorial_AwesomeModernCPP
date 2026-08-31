# CMakeLists.txt —— find_package 与 cxx_std_NN 的现代写法
#
# 这份文件演示两件事:
#   1. 用 target_compile_features 给 target 单独设 C++ 标准(现代写法)
#   2. 用 find_package 接入第三方库,拿到导入 target(现代写法)
#
# 配套文章: documents/vol7-engineering/ch00-cmake-fundamentals/03-find-package-and-cxx-standard.md

cmake_minimum_required(VERSION 3.20)
project(find_package_demo LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# ------------------------------------------------------------------
# C++ 标准:绑在 target 上的现代写法
# ------------------------------------------------------------------
# target_compile_features(app PRIVATE cxx_std_20) 的含义是:
#   "app 这个 target 要求至少 C++20"
# CMake 会根据编译器的默认标准决定是否要往编译命令里塞 -std 标志:
#   - 编译器默认 < C++20: 自动加 -std=c++20(或 gnu++20)
#   - 编译器默认 >= C++20: 不加标志(默认已经满足要求)
# 详见文章里 cxx_std_17/20/23 在 GCC 16 上的实测对比。
#
# 配套还可以关掉编译器扩展(CXX_EXTENSIONS OFF),强制走纯 -std=c++NN
# 而不是带 GNU 扩展的 -std=gnu++NN:

add_executable(app main.cpp)
target_compile_features(app PRIVATE cxx_std_20)
set_target_properties(app PROPERTIES CXX_EXTENSIONS OFF)

# ------------------------------------------------------------------
# 第三方库:find_package 的现代写法(可选,需要系统或 vcpkg/Conan 装好 fmt)
# ------------------------------------------------------------------
# 解开下面这段注释需要先有 fmt:
#   - Linux: 系统包管理器装(如 apt install libfmt-dev)
#   - 跨平台/可复现: 用 vcpkg 或 Conan,通过 toolchain 文件注入
#
# find_package(fmt REQUIRED) 会去 CMAKE_PREFIX_PATH 下的
#   <prefix>/lib/cmake/fmt/ 找 fmt-config.cmake,
# 找到后给你一个 fmt::fmt 的"导入 target"。
# 这个 target 身上挂好了 INTERFACE_INCLUDE_DIRECTORIES / IMPORTED_LOCATION 等
# 使用需求,你只需 target_link_libraries,include 路径和编译选项自动传过来。
#
# find_package(fmt REQUIRED)
# add_executable(app_with_fmt main.cpp)
# target_link_libraries(app_with_fmt PRIVATE fmt::fmt)
#
# 反模式对照(别这么写):
#   include_directories(${fmt_INCLUDE_DIRS})   # 目录级,污染所有 target
#   target_link_libraries(app PRIVATE ${fmt_LIBRARIES})  # 变量风格,不传播使用需求
