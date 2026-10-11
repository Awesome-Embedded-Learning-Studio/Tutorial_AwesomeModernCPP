// atomic_flag 自旋锁：test_and_set 是天生的「抢锁」原语——
// 谁把它从 0 翻成 1 谁持锁，别人自旋等待，持锁人 clear 放行。
// 教学实现：演示最小无锁互斥的构造，生产环境别拿它替换 mutex。
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

class Spinlock {
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;

  public:
    void lock() {
        // test_and_test_and_set：先只读地看一眼，看着像空的才去抢，
        // 省得每个等待者都往缓存行上写，把持锁人的核打脏
        while (flag_.test(std::memory_order_relaxed)) {
        }
        while (flag_.test_and_set(std::memory_order_acquire)) {
            while (flag_.test(std::memory_order_relaxed)) {
            }
        }
    }
    void unlock() { flag_.clear(std::memory_order_release); }
};

long long counter = 0; // 受锁保护的普通变量
Spinlock lock;

void worker() {
    for (int i = 0; i < 100000; ++i) {
        lock.lock();
        ++counter; // 临界区：普通自增，安全性全靠锁的 acquire/release
        lock.unlock();
    }
}

int main() {
    std::vector<std::jthread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back(worker);
    for (auto& t : threads)
        t.join();

    std::cout << "counter: " << counter << "\n";
    std::cout << "expected: " << 4 * 100000 << "\n";
    return 0;
}
