// 无名守卫的坑：lock_guard{mtx} 花括号临时对象白拿锁，语句末析构锁当场归还（另一个线程 try_lock
// 一摸就到）
#include <iostream>
#include <mutex>
#include <thread>

// 用另一个线程探一把锁是否仍被占用：try_lock 成功说明锁是空闲的。
bool probe_locked(std::mutex& m) {
    bool got = false;
    std::jthread probe([&] {
        got = m.try_lock();
        if (got) {
            m.unlock();
        }
    });
    probe.join();
    return !got; // true = 锁仍被原线程攥着
}

int main() {
    std::mutex mtx;
    std::lock_guard<std::mutex>{mtx}; // 临时对象！语句末析构，锁已释放
    // 编译这一行时，GCC 13+ 会给 [[nodiscard]] 警告：
    //   warning: ignoring return value of 'std::lock_guard<_Mutex>::lock_guard(mutex_type&)',
    //            declared with attribute 'nodiscard' [-Wunused-result]
    std::cout << "下一行用另一个线程探锁，free = " << !probe_locked(mtx)
              << "（1 = 锁确实白拿了）\n";
    return 0;
}
