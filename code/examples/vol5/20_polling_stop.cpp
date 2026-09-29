// 演示轮询式协作取消：停止检查写进循环条件，request_stop 后下一圈就退出（process_batch
// 为占位空操作）
#include <chrono>
#include <iostream>
#include <thread>

void process_batch(int /*iteration*/) {
    // 占位：真实的批处理活儿，这里什么都不做
}

void polling_worker(std::stop_token token) {
    int iteration = 0;
    while (!token.stop_requested()) { // 每圈查一次停止请求
        process_batch(iteration);     // 完整版见代码仓
        ++iteration;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "processed " << iteration << " batches\n";
}

int main() {
    std::jthread t(polling_worker); // token 自动注入
    std::this_thread::sleep_for(std::chrono::seconds(1));
    t.request_stop(); // 发出请求：下一圈循环条件就过不去了
    return 0;         // 析构再请求一次（幂等，无害），然后 join
}
