// 演示 thread_local 计数器：两个线程各自独立计数，地址互不相同
#include <cstdio>
#include <thread>

thread_local int counter = 0; // 常量初始化，构造上没有任何动作

void bump(const char* who) {
    for (int i = 0; i < 3; ++i) {
        ++counter;
        std::printf("%s: counter=%d，地址 %p\n", who, counter, (void*)&counter);
    }
}

int main() {
    std::thread t1(bump, "t1");
    std::thread t2(bump, "t2");
    t1.join();
    t2.join();
    std::printf("main: counter=%d，地址 %p\n", counter, (void*)&counter);
    return 0;
}
