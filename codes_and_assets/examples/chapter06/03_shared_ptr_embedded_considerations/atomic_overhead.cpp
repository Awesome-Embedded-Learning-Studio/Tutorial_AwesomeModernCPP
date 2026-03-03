// std::shared_ptr 原子操作开销示例
// 演示 shared_ptr 引用计数的原子操作特性及其性能影响

#include <memory>
#include <cstdio>
#include <thread>
#include <vector>
#include <chrono>

// 简单的测试对象
struct Counter {
    static int count;
    int id;
    Counter() : id(++count) {
        // 不输出以减少干扰
    }
    ~Counter() {
        // 不输出以减少干扰
    }
};

int Counter::count = 0;

// 测试单线程下的引用计数开销
void single_thread_test() {
    printf("=== Single Thread Test ===\n");

    const int N = 1000000;

    auto start = std::chrono::steady_clock::now();

    auto p = std::make_shared<Counter>();
    for (int i = 0; i < N; ++i) {
        auto p2 = p;  // 拷贝，原子递增
        // p2 离开作用域，原子递减
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    printf("%d copy/destroy cycles: %ld us\n", N, duration.count());
    printf("Average per operation: %.3f us\n", duration.count() / (2.0 * N));
}

// 测试多线程竞争
void multi_thread_test() {
    printf("\n=== Multi Thread Test ===\n");

    const int N = 100000;
    const int THREADS = 4;

    auto shared = std::make_shared<Counter>();

    auto worker = [&](int thread_id) {
        for (int i = 0; i < N; ++i) {
            auto local = shared;  // 多线程竞争原子递增
            // local 离开作用域，原子递减
        }
    };

    auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < THREADS; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    printf("%d threads x %d operations: %ld us\n", THREADS, N, duration.count());
    printf("Total operations: %d, time per op: %.3f us\n",
           THREADS * N * 2, duration.count() / (2.0 * THREADS * N));
}

// 对比测试：unique_ptr（无原子开销）
void unique_ptr_comparison() {
    printf("\n=== unique_ptr Comparison (No Atomic Overhead) ===\n");

    const int N = 1000000;

    auto start = std::chrono::steady_clock::now();

    auto p = std::make_unique<Counter>();
    for (int i = 0; i < N; ++i) {
        // unique_ptr 不可拷贝，只能移动
        // 这里演示移动语义的开销（几乎为零）
        auto p2 = std::move(p);
        p = std::move(p2);
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    printf("%d move cycles: %ld us\n", N, duration.count());
    printf("Average per operation: %.3f us\n", duration.count() / (2.0 * N));
}

// 演示原子内存序
void memory_order_demo() {
    printf("\n=== Memory Order Demo ===\n");

    // shared_ptr 的引用计数使用 acquire-release 内存序
    // 这确保了：
    // 1. 递增可以是 relaxed（只保证计数正确）
    // 2. 递减到 0 时需要 acquire-release（确保对象可见）

    auto sp = std::make_shared<Counter>();
    printf("Initial use_count: %ld\n", sp.use_count());

    {
        auto sp2 = sp;
        printf("After copy: use_count = %ld\n", sp.use_count());
    }
    printf("After sp2 destroyed: use_count = %ld\n", sp.use_count());
}

// 嵌入式注意事项
void embedded_notes() {
    printf("\n=== Embedded Considerations ===\n");
    printf("1. Atomic operations may not be lock-free on your platform\n");
    printf("2. Check with: std::atomic<int>::is_always_lock_free\n");
    printf("3. Lock-free atomics: %s\n",
           std::atomic<int>::is_always_lock_free ? "yes" : "no");
    printf("4. In ISR context: avoid shared_ptr entirely\n");
    printf("5. Consider single-threaded reference counting instead\n");
}

int main() {
    single_thread_test();
    multi_thread_test();
    unique_ptr_comparison();
    memory_order_demo();
    embedded_notes();

    printf("\n=== All Examples Complete ===\n");

    return 0;
}
