// 演示 stop_callback 的执行线程：回调里打印线程 id，与 main 的 id 一模一样——它不在 worker
// 自己的线程上跑
#include <chrono>
#include <iostream>
#include <stop_token>
#include <thread>

void worker(std::stop_token token) {
    int counter = 0;
    std::stop_callback cb(token, [&counter] {
        std::cout << "callback fired, counter = " << counter << " (on thread "
                  << std::this_thread::get_id() << ")\n";
    });

    while (!token.stop_requested()) {
        ++counter;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::cout << "worker exits\n";
}

int main() {
    std::cout << "main thread id = " << std::this_thread::get_id() << "\n";
    std::jthread t(worker);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    t.request_stop();
    return 0;
}
