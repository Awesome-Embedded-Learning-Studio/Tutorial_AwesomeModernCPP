// E4a:handler 与主循环共享的标志变量——volatile sig_atomic_t 的必要性
// 编译(两种各一遍,-O2 必开):
//   g++ -std=c++20 -Wall -Wextra -O2 -DUSE_VOLATILE flag_spin.cpp -o flag_spin_volatile
//   g++ -std=c++20 -Wall -Wextra -O2              flag_spin.cpp -o flag_spin_plain
// 跑法:timeout 3 ./flag_spin_plain  /  ./flag_spin_volatile
//
// 编排:SIGALRM 100ms 后把 g_flag 置 1。主循环忙等 g_flag。
//   volatile 版:每次迭代重新加载,handler 一置位循环立刻退出。
//   无 volatile 版:-O2 下编译器认为循环体内没人改 g_flag,把"读"提出循环,
//                  一次读完全程——handler 确实跑了、内存确实写了,循环就是看不见,
//                  timeout 杀进程(exit 124)。
// 旁证:flag_spin_plain_O2.asm 里主循环只剩一条 jmp(加载被提出循环外)。
#include <csignal>
#include <cstdio>
#include <sys/time.h>
#include <unistd.h>

#ifdef USE_VOLATILE
volatile sig_atomic_t g_flag = 0; // 正确写法:volatile 保证每次迭代重新加载
#else
sig_atomic_t g_flag = 0; // 反例:缺 volatile——sig_atomic_t 单独救不了
#endif

void on_alrm(int) {
    g_flag = 1;
}

int main() {
    struct sigaction sa{};
    sa.sa_handler = on_alrm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, nullptr);

    struct itimerval it{};
    it.it_value.tv_usec = 100 * 1000; // 100ms 后一次性
    setitimer(ITIMER_REAL, &it, nullptr);

    std::printf("busy-waiting for g_flag (SIGALRM at +100ms)...\n");
    fflush(stdout);
    long spins = 0;
    while (g_flag == 0)
        spins += 1; // 忙等:循环体不碰内存(只动寄存器),给编译器提出加载的自由
    std::printf("observed g_flag=1 after %ld spins -> clean exit\n", spins);
    return 0;
}
