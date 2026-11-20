// 线程与进程编号:同一个进程里的多条执行流,tid 各不相同
#include <print>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

static long my_tid() {
    return syscall(SYS_gettid);
}

int main() {
    std::println("main    : getpid = {}, tid = {}", getpid(), my_tid());
    std::thread t1([] { std::println("worker-1: getpid = {}, tid = {}", getpid(), my_tid()); });
    std::thread t2([] { std::println("worker-2: getpid = {}, tid = {}", getpid(), my_tid()); });
    t1.join();
    t2.join();
}
