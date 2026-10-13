// E3(翻车版):handler 里调用 printf——man 7 signal-safety 黑名单的实测
// 编译:g++ -std=c++20 -Wall -Wextra -O2 unsafe_printf.cpp -o unsafe_printf
// 跑法:./unsafe_printf > unsafe_printf_full.out   (stdout 重定向到文件 = 全缓冲,handler 与主循环
//        共享同一个 stdio 缓冲,交错直接写进缓冲,flush 时落盘)
//
// 编排:itimer 每 80us 发一次 SIGALRM(高频率,保证多个 tick 落在主循环 printf 的
// 执行窗口内),主循环 30000 行短行 printf。全程 ~几毫秒,几十个 tick。
// 预期乱象:handler 的 "[H ...]" 出现在主循环行的中间——主 printf 向缓冲拷到一半,
// 信号打断,handler 的 printf 接着往缓冲同一位置追加,随后主 printf 恢复继续拷。
#include <csignal>
#include <cstdio>
#include <sys/time.h>
#include <unistd.h>

volatile sig_atomic_t g_ticks = 0;

void on_alrm(int) {
    // 黑名单调用:printf 操作 stdio 的共享缓冲与锁,man 7 signal-safety 明确列为 unsafe
    // (g_ticks = g_ticks + 1 而非 ++:C++20 起对 volatile 的复合赋值/自增是弃用的)
    g_ticks = g_ticks + 1;
    std::printf("[H tick %d]", (int)g_ticks);
}

int main() {
    struct sigaction sa{};
    sa.sa_handler = on_alrm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // 只盯缓冲交错,别让 write 类调用被 EINTR 岔开话题
    sigaction(SIGALRM, &sa, nullptr);

    struct itimerval it{};
    it.it_value.tv_usec = 80;    // 80us 起振
    it.it_interval.tv_usec = 80; // 每 80us 一次
    setitimer(ITIMER_REAL, &it, nullptr);

    for (int i = 0; i < 30000; ++i)
        std::printf("M %06d abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ\n", i);

    it = {}; // 停表,别让收尾输出再被打断
    setitimer(ITIMER_REAL, &it, nullptr);

    // 收尾用 write(2) 直写:此时 printf 不可依赖
    char tail[128];
    int n = std::snprintf(tail, sizeof tail, "DONE ticks=%d\n", (int)g_ticks);
    write(STDOUT_FILENO, tail, n);
    return 0;
}
