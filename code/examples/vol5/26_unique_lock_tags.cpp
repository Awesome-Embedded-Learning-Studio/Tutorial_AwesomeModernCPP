// unique_lock 三种构造 tag：defer_lock 只包装不锁、adopt_lock 接管已持有的锁、主线程持锁时子线程
// try_to_lock 拿不到
#include <iostream>
#include <mutex>
#include <thread>

int main() {
    // defer_lock：只包装，不锁
    std::mutex m;
    {
        std::unique_lock<std::mutex> lk(m, std::defer_lock);
        std::cout << "defer_lock 构造后 owns_lock = " << lk.owns_lock() << '\n';
        lk.lock(); // 想锁的时候自己锁
        std::cout << "lock() 之后 owns_lock = " << lk.owns_lock() << '\n';
    }

    // adopt_lock：锁已经在手里（手动拿的），把看管责任接过来
    {
        std::mutex m2;
        m2.lock(); // 手动拿锁
        std::unique_lock<std::mutex> lk(m2, std::adopt_lock);
        std::cout << "adopt_lock 构造后 owns_lock = " << lk.owns_lock() << "（析构时替我们放）\n";
    }

    // try_to_lock：摸一下，拿不到不强求（拿不到时同线程再试是未定义行为，换线程演示）
    {
        std::mutex m3;
        m3.lock(); // 主线程先占住
        bool child_got = false;
        std::jthread t([&] {
            std::unique_lock<std::mutex> lk(m3, std::try_to_lock);
            child_got = lk.owns_lock();
        });
        t.join();
        std::cout << "主线程持锁时, 子线程 try_to_lock 拿到 = " << child_got << '\n';
        m3.unlock();
    }
    return 0;
}
