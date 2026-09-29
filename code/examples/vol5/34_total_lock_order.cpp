// 防线一：总锁序——两个线程都按 A、B 的顺序拿
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

std::mutex mtx_a;
std::mutex mtx_b;

void worker(int id) {
    std::lock_guard<std::mutex> a(mtx_a); // 谁都先拿 A
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> b(mtx_b); // 再拿 B
    std::cout << "worker " << id << ": 两把都到手\n";
}

int main() {
    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    t1.join();
    t2.join();
    std::cout << "程序正常收尾\n";
    return 0;
}
