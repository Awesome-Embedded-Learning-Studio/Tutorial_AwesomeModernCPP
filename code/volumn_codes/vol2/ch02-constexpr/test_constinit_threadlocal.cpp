// Test: Does constinit actually eliminate thread_local initialization checks?
#include <cstdio>
#include <chrono>

// Regular thread_local
thread_local int tl_counter = 42;

// constinit thread_local
constinit thread_local int tl_fast_counter = 42;

// Function to benchmark access
volatile int sink;

void benchmark_regular_thread_local(int iterations) {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        sink = tl_counter;
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    printf("Regular thread_local: %lld ns for %d iterations\n", duration.count(), iterations);
}

void benchmark_constinit_thread_local(int iterations) {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        sink = tl_fast_counter;
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    printf("constinit thread_local: %lld ns for %d iterations\n", duration.count(), iterations);
}

int main() {
    const int iterations = 10000000;
    benchmark_regular_thread_local(iterations);
    benchmark_constinit_thread_local(iterations);
    return 0;
}
