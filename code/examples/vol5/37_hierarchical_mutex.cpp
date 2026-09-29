// 层级锁：thread_local 记录本线程当前层级，越级上锁抛异常
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

class HierarchicalMutex {
  public:
    explicit HierarchicalMutex(unsigned long level) : level_(level) {}

    void lock() {
        check_violation();
        internal_.lock();
        push_level();
    }

    void unlock() {
        current_level_ = previous_level_; // 回到拿下这把锁之前的层级
        internal_.unlock();
    }

    bool try_lock() {
        check_violation();
        if (!internal_.try_lock()) {
            return false;
        }
        push_level();
        return true;
    }

  private:
    void check_violation() {
        if (level_ >= current_level_) {
            throw std::logic_error("锁层级越级：当前线程已持有同级或更低层级的锁");
        }
    }

    void push_level() {
        previous_level_ = current_level_;
        current_level_ = level_;
    }

    std::mutex internal_;
    const unsigned long level_;
    unsigned long previous_level_ = 0;
    static thread_local unsigned long current_level_;
};

thread_local unsigned long HierarchicalMutex::current_level_ =
    std::numeric_limits<unsigned long>::max();

HierarchicalMutex high_mutex(10000); // 应用层
HierarchicalMutex mid_mutex(5000);   // 业务层
HierarchicalMutex low_mutex(100);    // 底层 IO

int main() {
    // 合法路径：高 -> 中 -> 低，一路下行
    {
        std::lock_guard<HierarchicalMutex> h(high_mutex);
        std::lock_guard<HierarchicalMutex> m(mid_mutex);
        std::lock_guard<HierarchicalMutex> l(low_mutex);
        std::cout << "下行加锁：一路顺利\n";
    }

    // 越级路径：在低层里回头够中层，当场抛异常
    try {
        std::lock_guard<HierarchicalMutex> l(low_mutex);
        std::lock_guard<HierarchicalMutex> m(mid_mutex); // 5000 >= 100：违规
    } catch (const std::logic_error& e) {
        std::cout << "抓到越级: " << e.what() << '\n';
    }

    // 层级状态是 thread_local 的：上一个线程的状态不会漏到别的线程
    std::thread fresh([] {
        std::lock_guard<HierarchicalMutex> m(mid_mutex); // 新线程直接拿中层：合法
        std::cout << "新线程直接拿中层：合法\n";
    });
    fresh.join();
    return 0;
}
