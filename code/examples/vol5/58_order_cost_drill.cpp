// 内存序的差价在哪、不在哪，当场量给您看。
// 不在 RMW：x86 上 fetch_add 不管 relaxed 还是 seq_cst，编译出来都是同一条
//   lock addq——锁总线的价格照付，内存序一个子儿不多收。
// 在普通 store：relaxed 的 store 就是 mov，seq_cst 的 store 是 xchg，
//   xchg 带排空效果（store buffer 得清），每个都要贵上几倍。
// 计数与写入的最终值两种版都精确——内存序管的从来不是「加没加对」，
// 是「别的写读跟它排成什么关系」。
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

constexpr long kAddsPerThread = 20000000;
constexpr int kThreads = 8;
constexpr long kStores = 50000000;

template <std::memory_order MO> void counter_drill(const char* label) {
    std::atomic<long long> counter{0};
    auto start = std::chrono::steady_clock::now();

    std::vector<std::jthread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            for (long n = 0; n < kAddsPerThread; ++n)
                counter.fetch_add(1, MO);
        });
    }
    for (auto& t : threads)
        t.join();

    auto end = std::chrono::steady_clock::now();
    long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    std::cout << label << " counter: " << counter.load() << "  ("
              << ns / (kThreads * kAddsPerThread) << " ns/add)\n";
}

template <std::memory_order MO> void store_drill(const char* label) {
    std::atomic<long long> slot{0};
    auto start = std::chrono::steady_clock::now();
    for (long i = 0; i < kStores; ++i)
        slot.store(i, MO);
    auto end = std::chrono::steady_clock::now();
    long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    std::cout << label << " store x" << kStores << ": " << ns << " ns"
              << "  (" << static_cast<double>(ns) / kStores << " ns/store)\n";
}

int main() {
    counter_drill<std::memory_order_relaxed>("fetch_add relaxed");
    counter_drill<std::memory_order_seq_cst>("fetch_add seq_cst");
    store_drill<std::memory_order_relaxed>("store relaxed ");
    store_drill<std::memory_order_seq_cst>("store seq_cst ");
    return 0;
}
