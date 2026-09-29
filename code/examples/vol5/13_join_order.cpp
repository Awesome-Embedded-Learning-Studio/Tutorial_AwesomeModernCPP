// join 的顺序保证：waiting 先打，join 挂起一秒，joined 后落地
#include <chrono>
#include <iostream>
#include <thread>

void slow_work() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

int main() {
    std::thread t(slow_work);

    std::cout << "main: waiting\n";
    t.join();
    std::cout << "main: joined, worker is done\n";
    return 0;
}
