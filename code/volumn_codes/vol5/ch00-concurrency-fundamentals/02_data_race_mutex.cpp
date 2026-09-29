// 02_data_race_mutex.cpp —— 同一个计数器，用 mutex 把冲突访问隔开
// 编译（TSan 版）: g++ -fsanitize=thread -g -O2 -pthread 02_data_race_mutex.cpp -o fixed_tsan
#include <iostream>
#include <mutex>
#include <thread>

int counter = 0;
std::mutex counter_mtx;  // 一把锁看住 counter 的所有访问

void increment(int times)
{
    for (int i = 0; i < times; ++i) {
        std::lock_guard<std::mutex> lock(counter_mtx);
        ++counter;  // 现在这一次 ++ 与别的线程的 ++ 有了先后次序
    }
}

int main()
{
    constexpr int kTimes = 100000;
    std::thread t1(increment, kTimes);
    std::thread t2(increment, kTimes);
    t1.join();
    t2.join();
    std::cout << "counter = " << counter << "\n";  // 200000，TSan 报告干净，退出码 0
    return 0;
}
