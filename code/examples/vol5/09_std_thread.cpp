// std::thread basics: creation, join, detach, IDs
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

// 1. Function pointer
void print_hello(int id) {
    std::cout << "Hello from thread " << id << "\n";
}

int main() {
    std::cout << "=== Thread Creation Styles ===\n";

    // Function pointer
    std::thread t1(print_hello, 42);
    t1.join();

    // Lambda
    std::vector<int> data = {1, 2, 3, 4, 5};
    int sum = 0;
    std::thread t2([&data, &sum]() {
        for (int v : data) {
            sum += v;
        }
    });
    t2.join();
    std::cout << "Sum from lambda thread = " << sum << "\n";

    // Thread ID
    std::cout << "\n=== Thread IDs ===\n";
    std::thread t3(
        []() { std::cout << "Worker thread ID: " << std::this_thread::get_id() << "\n"; });
    std::cout << "Main thread ID: " << std::this_thread::get_id() << "\n";
    t3.join();

    // Hardware concurrency
    unsigned int cores = std::thread::hardware_concurrency();
    std::cout << "\nHardware concurrency: " << cores << "\n";

    // Basic parallel pattern: data partitioning
    std::cout << "\n=== Parallel Processing ===\n";
    constexpr size_t kDataSize = 1000;
    constexpr unsigned int kNumThreads = 4;

    std::vector<int> input(kDataSize);
    std::vector<int> output(kDataSize);
    for (size_t i = 0; i < kDataSize; ++i) {
        input[i] = static_cast<int>(i);
    }

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);
    size_t chunk = kDataSize / kNumThreads;

    for (unsigned int i = 0; i < kNumThreads; ++i) {
        size_t start = i * chunk;
        size_t end = (i == kNumThreads - 1) ? kDataSize : start + chunk;
        threads.emplace_back([&input, &output, start, end]() {
            for (size_t j = start; j < end; ++j) {
                output[j] = input[j] * input[j];
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "Processed " << kDataSize << " elements with " << kNumThreads << " threads\n";
    std::cout << "output[0]=" << output[0] << " output[999]=" << output[999] << "\n";

    return 0;
}
