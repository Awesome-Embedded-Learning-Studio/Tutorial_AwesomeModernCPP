// lock_guard 修复漏锁：4 线程各十万次自增收敛到期望值，临界区抛异常后锁照样被析构放掉
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

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
    // 计数器竞态收敛：异常也好、提前 return 也好，都不用惦记放锁
    long counter = 0;
    std::mutex counter_m;
    auto bump = [&] {
        std::lock_guard<std::mutex> lk(counter_m);
        ++counter;
    };
    std::vector<std::jthread> ts;
    for (int i = 0; i != 4; ++i) {
        ts.emplace_back([&] {
            for (int k = 0; k != 100000; ++k) {
                bump();
            }
        });
    }
    for (auto& t : ts) {
        t.join();
    }
    std::cout << "4 线程 x 100000 次自增, 结果 = " << counter << "（期望 400000）\n";

    // 临界区抛异常，析构照样放锁
    std::mutex throw_m;
    try {
        std::lock_guard<std::mutex> lk(throw_m);
        throw std::runtime_error("守卫还在，异常先走");
    } catch (const std::exception& e) {
        std::cout << "异常被接住: " << e.what() << '\n';
    }
    std::cout << "异常过后锁已被释放: " << !probe_locked(throw_m) << '\n';
    return 0;
}
