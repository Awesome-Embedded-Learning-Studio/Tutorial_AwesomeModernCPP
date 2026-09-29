// 演示线程 id：t 的 id 与 worker 自己报的一致，join 之后变成空壳的 id
#include <iostream>
#include <thread>

void worker() {
    std::cout << "worker 自己的 id: " << std::this_thread::get_id() << "\n";
}

int main() {
    std::thread t(worker);
    std::cout << "main 的 id: " << std::this_thread::get_id() << "\n";
    std::cout << "t 的 id:     " << t.get_id() << "\n";

    t.join();
    std::cout << "join 之后 t 的 id: " << t.get_id() << "\n";
    return 0;
}
