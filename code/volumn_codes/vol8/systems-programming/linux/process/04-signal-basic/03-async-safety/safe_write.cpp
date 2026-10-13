// E3(安全版):handler 里只做两件事——write(2) 一个常量串 + 设置 volatile sig_atomic_t 标志
// 编译:g++ -std=c++20 -Wall -Wextra -O2 safe_write.cpp -o safe_write
// 跑法:./safe_write > safe_write_full.out
//
// 与 unsafe_printf.cpp 同样的 itimer 频率、同样的主循环行。stdout 设为无缓冲:
// 主循环每次 printf 立刻落 fd,一行一次 write 系统调用(原子)。handler 的 write(2)
// 只能落在两次系统调用之间——行永远不会被劈开。
// write 之所以安全:它是一个系统调用,所有状态在内核侧,用户态没有会被打断的
// 共享缓冲/锁。printf 不安全:它操作 stdio 流内部的缓冲指针与计数器(还有锁),
// 打断点落在半更新状态上就是 unsafe_printf 里的交错。
#include <csignal>
#include <cstdio>
#include <sys/time.h>
#include <unistd.h>

volatile sig_atomic_t g_ticks = 0;

void on_alrm(int) {
    g_ticks += 1;                          // 标志:主循环稍后轮询
    write(STDOUT_FILENO, "[H tick]\n", 9); // write(2) 在 POSIX 异步信号安全表内
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0); // 主循环 printf 逐行直落 fd

    struct sigaction sa{};
    sa.sa_handler = on_alrm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, nullptr);

    struct itimerval it{};
    it.it_value.tv_usec = 80;
    it.it_interval.tv_usec = 80;
    setitimer(ITIMER_REAL, &it, nullptr);

    for (int i = 0; i < 30000; ++i)
        std::printf("M %06d abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ\n", i);

    it = {};
    setitimer(ITIMER_REAL, &it, nullptr);

    std::printf("DONE ticks=%d (flag observed by main loop: %d)\n", (int)g_ticks, (int)g_ticks);
    return 0;
}
