// 演示 stop_callback：停止请求发出的瞬间回调同步执行，且跑在调用 request_stop 的线程（main）上
#include <chrono>
#include <iostream>
#include <stop_token>
#include <thread>

void worker(std::stop_token token) {
    int counter = 0;
    std::stop_callback cb(token, [&counter] {
        // 注意：这段代码跑在调用 request_stop 的线程上（本例是 main），
        // 不在 worker 自己的线程上
        std::cout << "callback fired, counter = " << counter << "\n";
    });

    while (!token.stop_requested()) {
        ++counter;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::cout << "worker exits\n";
}

int main() {
    std::jthread t(worker);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    t.request_stop(); // 回调在这里同步执行完毕，这一行才返回
    return 0;
}
