cmake_minimum_required(VERSION 3.20)
project(cppcon2025_02 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(02-01-reverse-cpp 02-01-reverse-cpp.cpp)

# 验证代码：汇编输出对比
add_executable(02-00-add-demo 02-00-add-demo.cpp)

# 验证代码：AVX2/FMA matmul 性能对比（需要 AVX2+FMA 硬件支持）
add_executable(02-00-matmul-test 02-00-matmul-test.cpp)
target_compile_options(02-00-matmul-test PRIVATE -O3 -mavx2 -mfma)
add_executable(02-02-bit-lookup 02-02-bit-lookup.cpp)
add_executable(03-01-square-asm 03-01-square-asm.cpp)
add_executable(03-02-check-length 03-02-check-length.cpp)
add_executable(02-03-stl-generic 02-03-stl-generic.cpp)
add_executable(02-04-explicit-instantiation 02-04-explicit-instantiation.cpp)
add_executable(02-05-optional-and-then 02-05-optional-and-then.cpp)
add_executable(02-06-span-basics 02-06-span-basics.cpp)
add_executable(02-07-proposal-tracker 02-07-proposal-tracker.cpp)

# ABI 验证代码
add_executable(02-08-abi-square-x86 02-08-abi-square-x86.cpp)
add_executable(02-09-abi-sum-seven 02-09-abi-sum-seven.cpp)

# 05-03 和 05-04 只需要 C++ 编译器
add_executable(05-03-consteval-endian-fixed 05-03-consteval-endian-fixed.cpp)
add_executable(05-04-count-char-vec 05-04-count-char-vec.cpp)

# 05-02 是故意写错的代码，用于验证编译失败，不加入构建
# 编译命令：g++ -std=c++20 05-02-consteval-endian-broken.cpp
# 预期：编译失败，报错 reinterpret_cast 不能出现在常量表达式中

# 05-01 需要 ARM 交叉编译工具链，不加入主机构建
# 编译命令：arm-linux-gnueabihf-gcc -static 05-01-arm32-nv-condition.c -o test_arm
# 运行：qemu-arm ./test_arm

# 06-01 验证：constexpr 隐式 inline，头文件函数不违反 ODR
add_executable(06-01-header-inline-odr 06-01-header-inline-odr.cpp)

# 05 文章验证代码：Boost、Beman 与标准化路径
add_executable(05-05-shared-ptr-compare 05-05-shared-ptr-compare.cpp)
add_executable(05-06-filesystem-compare 05-06-filesystem-compare.cpp)
add_executable(05-07-optional-transform 05-07-optional-transform.cpp)
add_executable(05-08-span-demo 05-08-span-demo.cpp)
