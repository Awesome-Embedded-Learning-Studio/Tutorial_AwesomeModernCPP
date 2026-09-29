// 04_deadlock_reorder.cpp —— 双线程双锁反序：死锁的最小复现
// 两个线程各睡 50 毫秒再拿第二把锁，把交错窗口拉宽，基本一跑就锁死。
// 编译: g++ -Wall -Wextra -std=c++20 -O2 -pthread 04_deadlock_reorder.cpp -o deadlock
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

std::mutex mtx_a;
std::mutex mtx_b;

void thread1()
{
    std::lock_guard<std::mutex> a(mtx_a);
    std::cout << "thread1: locked A, waiting for B" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> b(mtx_b);  // 卡在这里：mtx_b 在 thread2 手里
    std::cout << "thread1: locked A and B" << std::endl;
}

void thread2()
{
    std::lock_guard<std::mutex> b(mtx_b);
    std::cout << "thread2: locked B, waiting for A" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> a(mtx_a);  // 顺序反了过来，谁也不放手
    std::cout << "thread2: locked A and B" << std::endl;
}

int main()
{
    std::thread t1(thread1);
    std::thread t2(thread2);
    t1.join();
    t2.join();
    return 0;
}
