cmake_minimum_required(VERSION 3.20)
project(chapter08_10_etl CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Embedded-friendly options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
# 警告旗标按编译器分派:MSVC 用 /W4,GCC/Clang 用 -Wall -Wextra(Windows 免 WSL 直接构建)
add_compile_options(
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra>"
)

# Add ETL include path (if you have ETL installed)
# include_directories(/path/to/etl/include)

# Note: This example requires the ETL library
# You can get it from: https://github.com/ETLCPP/etl

# Create executables for each example
add_executable(etl_vector_demo etl_vector_demo.cpp)
add_executable(etl_queue_demo etl_queue_demo.cpp)
add_executable(etl_pool_demo etl_pool_demo.cpp)

# Optional: disable exceptions and RTTI for embedded
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")
