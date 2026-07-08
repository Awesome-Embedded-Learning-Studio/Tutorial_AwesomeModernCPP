// AtomicFlag:一次性、release-Set / acquire-IsSet 的原子标志
// 来源:WeakPtr 前置知识(二):std::atomic 与 memory_order (pre-02)
// 编译:g++ -std=c++17 -Wall -Wextra 13_atomic_flag.cpp -o 13_atomic_flag -pthread

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

namespace {

// 对应 base::AtomicFlag:std::atomic<uint_fast8_t> 的语义收窄封装
// 一次性(无 public clear)+ 单序列写(DCHECK,这里省略)+ release/acquire
class AtomicFlag {
  public:
    void Set() noexcept {
        flag_.store(1, std::memory_order_release); // 发布"Set 之前的写"
    }
    bool IsSet() const noexcept {
        return flag_.load(std::memory_order_acquire) != 0; // acquire:建立 happens-before
    }

  private:
    std::atomic<uint_fast8_t> flag_{0};
};

} // namespace

int main() {
    std::cout << "=== AtomicFlag 基本行为 ===\n";
    AtomicFlag f;
    std::cout << "  初始 IsSet=" << f.IsSet() << "\n";
    f.Set();
    std::cout << "  Set 后 IsSet=" << f.IsSet() << "\n";

    std::cout << "\n=== release/acquire 建立跨线程 happens-before ===\n";
    // 线程 A:写 data,然后 Set(release)
    // 线程 B:IsSet(acquire)看到 true ⇒ 必然看到 data==42
    int data = 0;
    AtomicFlag ready;

    std::thread producer([&] {
        data = 42;   // (1) 普通写
        ready.Set(); // (2) release-store:把 (1) 发布出去
    });
    std::thread consumer([&] {
        while (!ready.IsSet()) {
        } // (3) acquire-load:等 release
        std::cout << "  consumer 看到 data=" << data << " (期望 42)\n"; // (4) 保证看到 (1)
    });
    producer.join();
    consumer.join();

    std::cout << "\n=== 对比 relaxed:不建立同步,可能读到旧值 ===\n";
    std::cout << "  (relaxed 只保证原子,不传递 data 的可见性 —— 本质上不能用来做 liveness flag)\n";
    std::cout
        << "  AtomicFlag 选 release/acquire 正是为了让 WeakPtr 的 IsValid 看到对象的全部状态\n";
    return 0;
}
