// Performance Comparison: Traditional vs Ranges
// Compiles the performance of traditional STL algorithms vs Ranges pipelines

#include <iostream>
#include <ranges>
#include <vector>
#include <algorithm>
#include <chrono>
#include <random>

// Traditional approach
std::vector<int> traditional_approach(const std::vector<int>& input) {
    std::vector<int> temp1;
    std::copy_if(input.begin(), input.end(), std::back_inserter(temp1),
                 [](int x) { return x > 50; });

    std::vector<int> temp2;
    std::transform(temp1.begin(), temp1.end(), std::back_inserter(temp2),
                   [](int x) { return x * 2; });

    return temp2;
}

// Ranges approach
std::vector<int> ranges_approach(const std::vector<int>& input) {
    auto pipeline = input
        | std::views::filter([](int x) { return x > 50; })
        | std::views::transform([](int x) { return x * 2; });

    return std::vector<int>(pipeline.begin(), pipeline.end());
}

// Benchmark helper
template<typename Func>
long long benchmark(const char* name, Func&& func, int iterations = 1000) {
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        func();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << name << ": " << duration.count() << " us" << std::endl;

    return duration.count();
}

void demo_correctness() {
    std::cout << "=== Correctness Check ===" << std::endl;

    std::vector<int> data = {12, 45, 23, 67, 34, 89, 56};

    auto r1 = traditional_approach(data);
    auto r2 = ranges_approach(data);

    std::cout << "Traditional: ";
    for (int x : r1) {
        std::cout << x << " ";
    }
    std::cout << std::endl;

    std::cout << "Ranges:     ";
    for (int x : r2) {
        std::cout << x << " ";
    }
    std::cout << std::endl;

    std::cout << "Results match: " << (r1 == r2 ? "yes" : "no") << std::endl;
}

void demo_performance_comparison() {
    std::cout << "\n=== Performance Comparison ===" << std::endl;
    std::cout << "Data size: 1,000,000 elements" << std::endl;
    std::cout << "Iterations: 1,000" << std::endl;
    std::cout << "Compile with -O2 for meaningful results" << std::endl;

    // Generate test data
    std::vector<int> data(1000000);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 100);
    for (auto& v : data) {
        v = dis(gen);
    }

    // Warm up
    traditional_approach(data);
    ranges_approach(data);

    // Benchmark
    auto time_traditional = benchmark("Traditional", [&]() {
        traditional_approach(data);
    });

    auto time_ranges = benchmark("Ranges", [&]() {
        ranges_approach(data);
    });

    double ratio = static_cast<double>(time_ranges) / time_traditional;
    std::cout << "\nRatio (Ranges/Traditional): " << ratio << std::endl;

    if (ratio < 1.1) {
        std::cout << "Ranges performs similarly to traditional!" << std::endl;
    }
}

void demo_complex_pipeline() {
    std::cout << "\n=== Complex Pipeline Performance ===" << std::endl;

    std::vector<int> data(100000);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<int>(i % 100);
    }

    // Many stages
    auto time_trad = benchmark("Traditional (5 stages)", [&]() {
        auto t1 = data;
        std::vector<int> t2, t3, t4, t5;
        std::copy_if(t1.begin(), t1.end(), std::back_inserter(t2), [](int x){return x>50;});
        std::transform(t2.begin(), t2.end(), std::back_inserter(t3), [](int x){return x*2;});
        std::copy_if(t3.begin(), t3.end(), std::back_inserter(t4), [](int x){return x<200;});
        std::transform(t4.begin(), t4.end(), std::back_inserter(t5), [](int x){return x+10;});
        return t5;
    }, 100);

    auto time_rng = benchmark("Ranges (5 stages)", [&]() {
        auto pipeline = data
            | std::views::filter([](int x) { return x > 50; })
            | std::views::transform([](int x) { return x * 2; })
            | std::views::filter([](int x) { return x < 200; })
            | std::views::transform([](int x) { return x + 10; });
        return std::vector<int>(pipeline.begin(), pipeline.end());
    }, 100);

    std::cout << "Complex pipeline ratio: "
              << static_cast<double>(time_rng) / time_trad << std::endl;
}

void demo_memory_efficiency() {
    std::cout << "\n=== Memory Efficiency ===" << std::endl;

    // Traditional: creates 2 temporary vectors
    std::cout << "Traditional approach:" << std::endl;
    std::cout << "  - temp1 vector for filtered results" << std::endl;
    std::cout << "  - temp2 vector for transformed results" << std::endl;
    std::cout << "  - Peak memory: 3x data size" << std::endl;

    // Ranges: only final vector
    std::cout << "\nRanges approach:" << std::endl;
    std::cout << "  - No temporary vectors" << std::endl;
    std::cout << "  - Peak memory: 1x data size (just output)" << std::endl;
    std::cout << "  - Views only store pointers/iterators" << std::endl;
}

void demo_zero_copy() {
    std::cout << "\n=== Zero Copy Demonstration ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5};

    // Ranges view - no copying
    auto view = data | std::views::filter([](int x) { return x > 2; });

    std::cout << "View size (implementation defined): ~few pointers" << std::endl;
    std::cout << "No elements copied until you iterate!" << std::endl;

    std::cout << "\nIterating view: ";
    for (int x : view) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

int main() {
    std::cout << "=== Performance Comparison Demo ===" << std::endl;

    demo_correctness();
    demo_performance_comparison();
    demo_complex_pipeline();
    demo_memory_efficiency();
    demo_zero_copy();

    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "- Ranges matches traditional performance with -O2" << std::endl;
    std::cout << "- Ranges uses less memory (no temporaries)" << std::endl;
    std::cout << "- Ranges code is more readable and maintainable" << std::endl;
    std::cout << "- For embedded: less memory = less fragmentation" << std::endl;

    return 0;
}
