// 手动 lock/unlock 的两条漏锁路径：提前 return 与临界区抛异常，锁都漏了（用另一个线程的 try_lock
// 取证）
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

// 用另一个线程探一把锁是否仍被占用：try_lock 成功说明锁是空闲的。
// 谁拿的锁谁放，所以探针线程自己在 lambda 里配平。
bool probe_locked(std::mutex& m) {
    bool got = false;
    std::jthread probe([&] {
        got = m.try_lock();
        if (got) {
            m.unlock();
        }
    });
    probe.join(); // 必须等探针干完活再读 got，不然读到的是初始值
    return !got;  // true = 锁仍被原线程攥着
}

int main() {
    // 路径 A：提前 return，写在后面的 unlock 被跳过
    std::mutex early_ret_m;
    auto early_return_path = [&early_ret_m] {
        early_ret_m.lock();
        if (true) {
            return; // 漏：unlock 在 return 之后，永远执行不到
        }
        early_ret_m.unlock();
    };
    early_return_path();
    const bool leaked_a = probe_locked(early_ret_m);
    std::cout << "提前 return 后锁仍被持有: " << leaked_a << '\n';
    early_ret_m.unlock(); // 锁的主人还是本线程，自己放才合法

    // 路径 B：临界区里抛异常，手动 unlock 没机会跑
    std::mutex throw_m;
    try {
        throw_m.lock();
        throw std::runtime_error("临界区里出了事");
        // throw_m.unlock();  // 写了也到不了
    } catch (const std::exception& e) {
        std::cout << "异常被接住: " << e.what() << '\n';
    }
    const bool leaked_b = probe_locked(throw_m);
    std::cout << "异常路径后锁仍被持有: " << leaked_b << '\n';
    throw_m.unlock(); // 同上，本线程自救
    return 0;
}
