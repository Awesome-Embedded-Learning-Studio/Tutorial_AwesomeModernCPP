cmake_minimum_required(VERSION 3.20)
project(ch02_constexpr VERSION 0.0.1 LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(constexpr_basics_crc_benchmark constexpr_basics_crc_benchmark.cpp)
add_executable(constexpr_ctor_types constexpr_ctor_types.cpp)
add_executable(consteval_constinit consteval_constinit.cpp)
add_executable(compile_time_practice compile_time_practice.cpp)
add_executable(constexpr_limits_test constexpr_limits_test.cpp)
# 512/520 层 constexpr 递归 + fib(30) 的 ~270 万次调用超出 MSVC 默认求值限制
# (C2131);GCC/Clang 默认限制宽,无需(也无)对应选项
target_compile_options(constexpr_limits_test PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/constexpr:depth1024;/constexpr:steps67108864>")
add_executable(constexpr_immediate_value_test constexpr_immediate_value_test.cpp)
add_executable(const_vs_constexpr_test const_vs_constexpr_test.cpp)
add_executable(constexpr_ctor_verify constexpr_ctor_verify.cpp)

# Test code for consteval/constinit verification
add_executable(test_constexpr_degradation test_constexpr_degradation.cpp)
add_executable(test_constinit_threadlocal test_constinit_threadlocal.cpp)
add_executable(test_consteval_pointer test_consteval_pointer.cpp)
add_executable(test_if_consteval test_if_consteval.cpp)
target_compile_features(test_if_consteval PUBLIC cxx_std_23)

# Test code for compile-time practice verification (article 04)
add_executable(test04_crc_table test04_crc_table.cpp)
add_executable(test04_sin_table test04_sin_table.cpp)
add_executable(test04_string_hash test04_string_hash.cpp)
add_executable(test04_state_machine test04_state_machine.cpp)
add_executable(test04_strategy_pattern test04_strategy_pattern.cpp)
add_executable(test04_baud_rate test04_baud_rate.cpp)

# Link math library for sin table test
# m(libm)是 POSIX 链接惯例;MSVC 的数学函数在 CRT 内置,不需要
if(NOT MSVC)
    target_link_libraries(test04_sin_table PRIVATE m)
endif()
