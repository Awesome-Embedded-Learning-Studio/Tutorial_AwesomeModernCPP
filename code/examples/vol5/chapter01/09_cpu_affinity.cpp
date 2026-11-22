// sched_setaffinity 绑核:经 native_handle 拿 pthread_t,用 POSIX 接口绑固定核心
// 在线沙箱只有 2 个逻辑核,这里绑 cpu 0;本地机器可改绑其他核心
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include <print>
#include <pthread.h>
#include <sched.h>
#include <thread>

int main() {
    std::thread t([] {
        for (int i = 0; i < 3; ++i)
            std::println("worker on cpu {}", sched_getcpu());
    });
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    pthread_t native = t.native_handle();
    int rc = pthread_setaffinity_np(native, sizeof(set), &set);
    std::println("pin worker to cpu 0 via native_handle, rc = {}", rc);
    t.join();
}
