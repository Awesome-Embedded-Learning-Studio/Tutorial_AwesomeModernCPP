// 演示既不 join 也不 detach 就析构：程序以 SIGABRT 崩溃，退出码 134，这是预期行为
#include <thread>

void some_work() {}

int main() {
    std::thread t(some_work);
    // 既没有 join()，也没有 detach()
    return 0;
}
