// data race 现场：两个线程各给普通 int 自增两百万次，期望 4000000，实际丢一大截
#include <iostream>
#include <thread>

int counter = 0; // 普通 int：非 atomic，也没有锁保护

void increment(int times) {
    for (int i = 0; i < times; ++i) {
        ++counter; // 读 -> 加 -> 写，三步
    }
}

int main() {
    constexpr int kTimes = 2'000'000;
    std::thread t1(increment, kTimes);
    std::thread t2(increment, kTimes);
    t1.join();
    t2.join();
    std::cout << "counter = " << counter << "\n";
    return 0;
}
