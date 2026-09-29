// 演示组控制：一个外部 stop_source 派生的 token 显式传给四个 jthread，一次 request_stop 全组收工
#include <chrono>
#include <iostream>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;

void worker_fun(int id, std::stop_token stoken) {
    // token 排在形参表后面：给自动注入让路的机关就在这里——
    // 注入形式是 f(token, id, 外部token) 三个实参，与本签名对不上，
    // 探测退到"原样调用"，咱们显式传的 token 畅通无阻
    while (!stoken.stop_requested()) {
        std::cout << "worker " << id << " is working\n";
        std::this_thread::sleep_for(200ms);
    }
    std::cout << "worker " << id << " exits\n";
}

int main() {
    std::stop_source source; // 外部的 source，停止权握在 main 手里
    std::jthread threads[4];

    for (int i = 0; i < 4; ++i) {
        // token 作为尾参显式传入：四个线程共享同一份停止状态
        threads[i] = std::jthread(worker_fun, i + 1, source.get_token());
    }

    std::this_thread::sleep_for(1s);
    source.request_stop(); // 一次请求，四个线程一起看见
    return 0;              // 数组析构：每个 jthread 各自收尾（内部 source 的请求
                           // 没人理会，等于多发一次；join 逐个照做）
}
