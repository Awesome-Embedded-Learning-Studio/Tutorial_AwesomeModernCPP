// 每条线程的栈有多大:pthread_getattr_np 查实际栈大小
#include <cstddef>
#include <print>
#include <pthread.h>
#include <thread>

static void report(const char* who) {
    pthread_attr_t attr;
    void* addr = nullptr;
    size_t size = 0;
    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, &addr, &size);
    std::println("{} stack size = {} KB", who, size / 1024);
    pthread_attr_destroy(&attr);
}

int main() {
    report("main  ");
    std::thread t([] { report("worker"); });
    t.join();
}
