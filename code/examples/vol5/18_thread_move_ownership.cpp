// 演示 std::thread 的 move-only 所有权：move 之后源对象变成空壳（joinable 为假）
#include <iostream>
#include <thread>

void worker() {
    std::cout << "worker running\n";
}

int main() {
    std::thread t1(worker);
    std::cout << "t1 joinable: " << t1.joinable() << "\n"; // 1

    std::thread t2 = std::move(t1);                        // 所有权从 t1 移交给 t2
    std::cout << "t1 joinable: " << t1.joinable() << "\n"; // 0
    std::cout << "t2 joinable: " << t2.joinable() << "\n"; // 1

    t2.join(); // 此后能收尾的只有 t2
    return 0;
}
