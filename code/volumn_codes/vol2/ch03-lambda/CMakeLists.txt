cmake_minimum_required(VERSION 3.15)
project(lambda_basics_tests LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 测试 lambda 内联和性能开销
add_executable(test_lambda_inlining
    test_lambda_inlining.cpp
)

# 测试返回类型推导规则
add_executable(test_return_type_deduction
    test_return_type_deduction.cpp
)

# 启用优化以便更好地观察内联效果
target_compile_options(test_lambda_inlining PRIVATE -O2)
target_compile_options(test_return_type_deduction PRIVATE -O2)

# 测试捕获方式对闭包大小的影响
add_executable(test_capture_size
    test_capture_size.cpp
)

# 验证引用捕获的底层实现
add_executable(test_ref_capture_impl
    test_ref_capture_impl.cpp
)

# 验证 C++20 默认捕获行为变化
add_executable(test_cxx20_default_capture
    test_cxx20_default_capture.cpp
)

# lambda 性能基准测试
add_executable(benchmark_performance
    benchmark_performance.cpp
)

# 启用所有警告
target_compile_options(test_lambda_inlining PRIVATE -Wall -Wextra)
target_compile_options(test_return_type_deduction PRIVATE -Wall -Wextra)
target_compile_options(test_capture_size PRIVATE -Wall -Wextra)
target_compile_options(test_ref_capture_impl PRIVATE -Wall -Wextra)
target_compile_options(test_cxx20_default_capture PRIVATE -Wall -Wextra)
target_compile_options(benchmark_performance PRIVATE -Wall -Wextra)

# 性能测试使用 -O3 优化
target_compile_options(benchmark_performance PRIVATE -O3)

# 测试递归 lambda 不同实现的性能差异
add_executable(test_recursive_lambda_performance
    test_recursive_lambda_performance.cpp
)
target_compile_options(test_recursive_lambda_performance PRIVATE -O2 -Wall -Wextra)

# 测试 Concepts 与传统 SFINAE 的错误信息质量
add_executable(test_concepts_error_messages
    test_concepts_error_messages.cpp
)
target_compile_options(test_concepts_error_messages PRIVATE -Wall -Wextra -std=c++20)

# 测试 std::function SBO 大小
add_executable(test_sbo_size
    test_sbo_size.cpp
)
target_compile_options(test_sbo_size PRIVATE -Wall -Wextra)

# 测试 std::invoke 性能
add_executable(test_invoke_performance
    test_invoke_performance.cpp
)
target_compile_options(test_invoke_performance PRIVATE -O2 -Wall -Wextra)

# 测试 std::function 性能
add_executable(test_function_performance
    test_function_performance.cpp
)
target_compile_options(test_function_performance PRIVATE -O2 -Wall -Wextra)

# 测试函数式编程模式 - 高阶函数
add_executable(test_higher_order_functions
    test_higher_order_functions.cpp
)
target_compile_options(test_higher_order_functions PRIVATE -O2 -Wall -Wextra)

# 测试函数组合
add_executable(test_function_composition
    test_function_composition.cpp
)
target_compile_options(test_function_composition PRIVATE -O2 -Wall -Wextra)
target_compile_features(test_function_composition PRIVATE cxx_std_20)

# 测试偏应用
add_executable(test_partial_application
    test_partial_application.cpp
)
target_compile_options(test_partial_application PRIVATE -O2 -Wall -Wextra)

# 测试 map/filter/reduce 模式
add_executable(test_map_filter_reduce
    test_map_filter_reduce.cpp
)
target_compile_options(test_map_filter_reduce PRIVATE -O2 -Wall -Wextra)

# 测试 Ranges 性能
add_executable(test_ranges_performance
    test_ranges_performance.cpp
)
target_compile_options(test_ranges_performance PRIVATE -O3 -Wall -Wextra)
target_compile_features(test_ranges_performance PRIVATE cxx_std_20)

# 测试不可变数据和 NRVO
add_executable(test_immutability_nrvo
    test_immutability_nrvo.cpp
)
target_compile_options(test_immutability_nrvo PRIVATE -O2 -Wall -Wextra)

# 如果需要生成汇编进行内联分析，可以运行：
# cmake --build . --target test_lambda_inlining
# objdump -d test_lambda_inlining | grep -A20 "call_func"
