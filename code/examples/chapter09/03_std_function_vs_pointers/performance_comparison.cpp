// Performance Comparison: Function Pointer vs std::function
// Demonstrates the performance differences between these approaches

#include <iostream>
#include <functional>
#include <chrono>
#include <vector>
#include <algorithm>

// Test function
int add(int a, int b) {
    return a + b;
}

// Direct call baseline
int test_direct(int iterations) {
    volatile int sum = 0;  // volatile to prevent optimization
    for (int i = 0; i < iterations; ++i) {
        sum += add(i, i + 1);
    }
    return sum;
}

// Function pointer call
int test_function_pointer(int iterations, int (*func)(int, int)) {
    volatile int sum = 0;
    for (int i = 0; i < iterations; ++i) {
        sum += func(i, i + 1);
    }
    return sum;
}

// std::function call
int test_std_function(int iterations, std::function<int(int, int)> func) {
    volatile int sum = 0;
    for (int i = 0; i < iterations; ++i) {
        sum += func(i, i + 1);
    }
    return sum;
}

// Lambda with auto (template)
template<typename Func>
int test_lambda_template(int iterations, Func&& func) {
    volatile int sum = 0;
    for (int i = 0; i < iterations; ++i) {
        sum += func(i, i + 1);
    }
    return sum;
}

// Benchmark helper
template<typename Func>
long long benchmark(const char* name, Func&& func, int iterations = 10000000) {
    auto start = std::chrono::high_resolution_clock::now();
    func(iterations);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << name << ": " << duration.count() << " us";
    return duration.count();
}

int main() {
    std::cout << "=== Performance Comparison Demo ===" << std::endl;
    std::cout << "Compile with -O2 for meaningful results" << std::endl;
    std::cout << "Iterations: 10,000,000\n" << std::endl;

    const int iterations = 10000000;

    // Warm up
    test_direct(1000);
    test_function_pointer(1000, add);
    test_std_function(1000, add);
    test_lambda_template(1000, [](int a, int b) { return a + b; });

    // Benchmark
    auto time_direct = benchmark("Direct call", [&](int) {
        test_direct(iterations);
    });

    auto time_fp = benchmark("Function pointer", [&](int) {
        test_function_pointer(iterations, add);
    });

    auto time_sf = benchmark("std::function", [&](int) {
        test_std_function(iterations, add);
    });

    auto time_sf_lambda = benchmark("std::function (lambda)", [&](int) {
        test_std_function(iterations, [](int a, int b) { return a + b; });
    });

    auto time_template = benchmark("Template (lambda)", [&](int) {
        test_lambda_template(iterations, [](int a, int b) { return a + b; });
    });

    std::cout << "\n--- Summary ---" << std::endl;
    std::cout << "Function pointer vs Direct: "
              << (100.0 * time_fp / time_direct) << "%" << std::endl;
    std::cout << "std::function vs Direct: "
              << (100.0 * time_sf / time_direct) << "%" << std::endl;
    std::cout << "std::function (lambda) vs Direct: "
              << (100.0 * time_sf_lambda / time_direct) << "%" << std::endl;
    std::cout << "Template vs Direct: "
              << (100.0 * time_template / time_direct) << "%" << std::endl;

    // Size comparison
    std::cout << "\n--- Size Comparison ---" << std::endl;
    std::cout << "sizeof(int(*)(int,int)): " << sizeof(int(*)(int,int)) << " bytes" << std::endl;
    std::cout << "sizeof(std::function<int(int,int)>): "
              << sizeof(std::function<int(int,int)>) << " bytes" << std::endl;

    // Practical example: sorting
    std::cout << "\n--- Practical Example: Sorting ---" << std::endl;

    std::vector<int> data1(1000000);
    for (size_t i = 0; i < data1.size(); ++i) {
        data1[i] = static_cast<int>(i % 1000);
    }

    std::vector<int> data2 = data1;

    auto start1 = std::chrono::high_resolution_clock::now();
    std::sort(data1.begin(), data1.end(), [](int a, int b) { return a < b; });
    auto end1 = std::chrono::high_resolution_clock::now();

    auto start2 = std::chrono::high_resolution_clock::now();
    std::sort(data2.begin(), data2.end(), std::less<int>{});
    auto end2 = std::chrono::high_resolution_clock::now();

    auto time_sort_lambda = std::chrono::duration_cast<std::chrono::microseconds>(end1 - start1);
    auto time_sort_function = std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start2);

    std::cout << "Sort with lambda: " << time_sort_lambda.count() << " us" << std::endl;
    std::cout << "Sort with std::less: " << time_sort_function.count() << " ms" << std::endl;

    std::cout << "\n=== Conclusion ===" << std::endl;
    std::cout << "- Function pointers: Zero overhead after optimization" << std::endl;
    std::cout << "- std::function: Small overhead, but enables context capture" << std::endl;
    std::cout << "- Template (auto): Zero overhead, best choice when possible" << std::endl;
    std::cout << "- Use function pointers for hot paths" << std::endl;
    std::cout << "- Use std::function when you need context capture" << std::endl;
    std::cout << "- Use templates for compile-time polymorphism" << std::endl;

    return 0;
}
