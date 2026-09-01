cmake_minimum_required(VERSION 3.21)
project(presets_demo LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 兜底:CMakePresets.json 已通过 cacheVariables 设了这几个值,
# 这里再 set 一遍是为了支持"不用 preset、直接 cmake -B build"的老式姿势
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 用 NDEBUG 区分 Debug / Release,方便验证 CMAKE_BUILD_TYPE 真的流到了编译命令
add_executable(app main.cpp)
