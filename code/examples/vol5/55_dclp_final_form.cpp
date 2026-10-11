// DCLP（双检查锁定）的正确终形与它的现代替身。
// 手写版：指针用 atomic 装，门外 acquire 快查，拿锁门里再查、release 发布。
// 对照版：块级 static（Meyers 单例），C++11 起标准保证初始化恰好一次。
// 八个线程同时抢两种单例的初始化，各自记录拿到的地址——全部相同才算对。
#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

class ManualDclp {
    inline static std::atomic<ManualDclp*> instance_{nullptr};
    inline static std::mutex ctor_mutex_;

    ManualDclp() { std::cout << "ManualDclp constructed once\n"; }

  public:
    static ManualDclp* get() {
        // 第一道检查：多数调用走到这里就返回，不碰锁。
        // acquire 与门里的 release store 配对：看见非空指针，就看见了完整的构造
        ManualDclp* p = instance_.load(std::memory_order_acquire);
        if (p == nullptr) {
            std::lock_guard<std::mutex> guard(ctor_mutex_);
            // 第二道检查：拿锁后再看一眼，先抢到锁的那个线程已经把活干完了
            p = instance_.load(std::memory_order_relaxed);
            if (p == nullptr) {
                p = new ManualDclp();
                // release 发布：构造的写先落地，指针后可见
                instance_.store(p, std::memory_order_release);
            }
        }
        return p;
    }
    int value = 42;
};

class MeyersSingleton {
    MeyersSingleton() { std::cout << "MeyersSingleton constructed once\n"; }

  public:
    static MeyersSingleton& get() {
        // 块级 static：标准保证线程安全的恰好一次初始化，
        // 手写版的 acquire/release 机关它全包了
        static MeyersSingleton instance;
        return instance;
    }
    int value = 42;
};

int main() {
    std::vector<std::jthread> threads;
    std::atomic<bool> all_same_manual{true};
    std::atomic<bool> all_same_meyers{true};

    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&] {
            ManualDclp* m1 = ManualDclp::get();
            ManualDclp* m2 = ManualDclp::get();
            if (m1 != m2)
                all_same_manual = false;

            MeyersSingleton& s1 = MeyersSingleton::get();
            MeyersSingleton& s2 = MeyersSingleton::get();
            if (&s1 != &s2)
                all_same_meyers = false;
        });
    }
    for (auto& t : threads)
        t.join();

    std::cout << "manual dclp single address: " << (all_same_manual ? "yes" : "NO") << "\n";
    std::cout << "meyers singleton single address: " << (all_same_meyers ? "yes" : "NO") << "\n";
    return 0;
}
