// 演示 detach 放走线程后进程先退：cleanup 那行多半不会出现，因为进程先退了
#include <chrono>
#include <iostream>
#include <thread>

void background_cleanup() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "cleanup: done\n";
}

int main() {
    std::thread t(background_cleanup);
    t.detach();

    std::cout << "main: detached, leaving now\n";
    return 0;
}
