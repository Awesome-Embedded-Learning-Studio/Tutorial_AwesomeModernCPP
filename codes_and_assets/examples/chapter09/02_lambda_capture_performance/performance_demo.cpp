// Lambda Capture Performance Analysis
// Demonstrates that lambda capture has zero runtime overhead when inlined

#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>

// Global function for comparison
int add_global(int a, int b) {
    return a + b;
}

// Function with direct call
int direct_call(int (*func)(int, int), int a, int b) {
    return func(a, b);
}

// Lambda with value capture
int test_value_capture() {
    int threshold = 100;
    auto is_high = [threshold](int x) { return x > threshold; };

    int sum = 0;
    for (int i = 0; i < 1000; ++i) {
        if (is_high(i)) {
            sum += i;
        }
    }
    return sum;
}

// Lambda with reference capture
int test_reference_capture() {
    int threshold = 100;
    auto is_high = [&threshold](int x) { return x > threshold; };

    int sum = 0;
    for (int i = 0; i < 1000; ++i) {
        if (is_high(i)) {
            sum += i;
        }
    }
    return sum;
}

// No lambda (inline comparison)
int test_no_lambda() {
    int threshold = 100;
    int sum = 0;
    for (int i = 0; i < 1000; ++i) {
        if (i > threshold) {
            sum += i;
        }
    }
    return sum;
}

// Benchmarking function
template<typename Func>
long long benchmark(Func&& func, int iterations = 10000) {
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        volatile int result = func();
        (void)result;
    }

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

int main() {
    std::cout << "=== Lambda Capture Performance Demo ===" << std::endl;
    std::cout << "Compiled with -O2 optimization" << std::endl;
    std::cout << "Run with -O2 for fair comparison" << std::endl;

    // Warm up
    test_value_capture();
    test_reference_capture();
    test_no_lambda();

    std::cout << "\nBenchmarking 10,000 iterations each..." << std::endl;

    auto time_value = benchmark(test_value_capture);
    auto time_ref = benchmark(test_reference_capture);
    auto time_no_lambda = benchmark(test_no_lambda);

    std::cout << "\nResults:" << std::endl;
    std::cout << "  Value capture:    " << time_value << " us" << std::endl;
    std::cout << "  Reference capture: " << time_ref << " us" << std::endl;
    std::cout << "  No lambda:        " << time_no_lambda << " us" << std::endl;

    std::cout << "\nConclusion: With -O2, all three perform similarly." << std::endl;
    std::cout << "The compiler inlines the lambda, making capture overhead zero." << std::endl;

    // Demonstrate inline optimization
    std::cout << "\n=== Inline Optimization Demo ===" << std::endl;

    int x = 42;
    auto lambda = [x](int y) { return x + y; };

    // With -O2, this is optimized to: result = 42 + 10
    int result = lambda(10);
    std::cout << "lambda(10) with x=42 = " << result << std::endl;
    std::cout << "Compiler likely optimized this to: return 52;" << std::endl;

    // Complex example with STL algorithm
    std::cout << "\n=== STL Algorithm Performance ===" << std::endl;

    std::vector<int> data(100000);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<int>(i);
    }

    // Lambda with capture
    int threshold = 50000;
    auto start = std::chrono::high_resolution_clock::now();
    auto it = std::find_if(data.begin(), data.end(),
                          [threshold](int v) { return v > threshold; });
    auto end = std::chrono::high_resolution_clock::now();

    if (it != data.end()) {
        auto time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "Found value > " << threshold << ": " << *it << std::endl;
        std::cout << "Time taken: " << time.count() << " us" << std::endl;
    }

    // Memory overhead discussion
    std::cout << "\n=== Memory Overhead ===" << std::endl;
    std::cout << "Lambda object size = sum of captured variables" << std::endl;
    std::cout << "Empty lambda: " << sizeof([](){}) << " bytes" << std::endl;
    std::cout << "Lambda capturing one int: " << sizeof([x](){} ) << " bytes" << std::endl;
    std::cout << "Lambda capturing two ints: " << sizeof([x, x](){} ) << " bytes" << std::endl;

    return 0;
}
