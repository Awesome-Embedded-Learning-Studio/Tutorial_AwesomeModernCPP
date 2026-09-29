// 01_data_race.cpp —— ch00/02 的招牌坏例子：两个线程裸 ++ 一个全局 int
// 编译（普通版）:  g++ -Wall -Wextra -std=c++20 -O2 -pthread 01_data_race.cpp -o race
// 编译（TSan 版）: g++ -fsanitize=thread -g -O2 -pthread 01_data_race.cpp -o race_tsan
#include <iostream>
#include <thread>

int counter = 0;  // 普通 int，非 atomic，没有任何锁保护

void increment(int times)
{
    for (int i = 0; i < times; ++i) {
        ++counter;  // 读 -> 加 -> 写，三步不是原子的
    }
}

int main()
{
    constexpr int kTimes = 100000;
    std::thread t1(increment, kTimes);
    std::thread t2(increment, kTimes);
    t1.join();
    t2.join();
    std::cout << "counter = " << counter << "\n";  // 期望 200000，实际听调度与优化的
    return 0;
}
