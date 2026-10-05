// E4b RLIMIT_CPU:软限 1s -> SIGXCPU(可捕获,是"警告");硬限 2s -> SIGKILL(不可救)
//   满载循环里主线程定期报进度,handler 报自己的到达时刻;进程被 SIGKILL 处决是它
//   自己打印不了的,由 e4_cpu_run.sh 补记退出码
// 编译: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o e4_cpu e4_cpu.cpp
// 运行: ./e4_cpu_run.sh > e4_cpu.out 2>&1   (整轮 ~2s)
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

static void now(long* wall_ms, long* cpu_ms) {
    timespec w{}, c{};
    clock_gettime(CLOCK_MONOTONIC, &w);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &c);
    *wall_ms = w.tv_sec * 1000 + w.tv_nsec / 1000000;
    *cpu_ms = c.tv_sec * 1000 + c.tv_nsec / 1000000;
}
static long t0wall = 0, t0cpu = 0;
static volatile sig_atomic_t hits = 0;

static void xcpu(int) {
    hits = hits + 1; // volatile 对象在 C++20 里禁用 ++,拆开写
    long w, c;
    now(&w, &c);
    char buf[128];
    size_t n = 0;
    auto put = [&](const char* s) {
        while (*s)
            buf[n++] = *s++;
    };
    auto putnum = [&](long v) {
        char tmp[24];
        int i = 0;
        bool neg = v < 0;
        unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
        do {
            tmp[i++] = char('0' + u % 10);
            u /= 10;
        } while (u);
        if (neg)
            tmp[i++] = '-';
        while (i)
            buf[n++] = tmp[--i];
    };
    put("[SIGXCPU] wall=+");
    putnum(w - t0wall);
    put("ms cpu=+");
    putnum(c - t0cpu);
    put("ms (第 ");
    putnum(hits);
    put(" 次命中;软限是警告,硬限 2s 将是 SIGKILL)\n");
    ssize_t wr = write(STDOUT_FILENO, buf, n);
    (void)wr;
}

int main() {
    long w, c;
    now(&w, &c);
    t0wall = w;
    t0cpu = c;

    printf("== E4b RLIMIT_CPU: 软限 1s = SIGXCPU(警告) / 硬限 2s = SIGKILL(处决) ==\n");
    rlimit rl{};
    getrlimit(RLIMIT_CPU, &rl);
    if (rl.rlim_cur == RLIM_INFINITY)
        printf("初始: rlim_cur=unlimited rlim_max=unlimited\n");
    else
        printf("初始: rlim_cur=%lus rlim_max=%lus\n", (unsigned long)rl.rlim_cur,
               (unsigned long)rl.rlim_max);

    struct sigaction sa{};
    sa.sa_handler = xcpu;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGXCPU, &sa, nullptr);

    rl.rlim_cur = 1;
    rl.rlim_max = 2;
    if (setrlimit(RLIMIT_CPU, &rl) != 0) {
        perror("setrlimit");
        return 1;
    }
    printf("setrlimit(软 1s, 硬 2s) 后进入满载循环:\n");
    fflush(stdout);

    volatile unsigned long sum = 0;
    for (;;) {
        for (int i = 0; i < 200000000; ++i)
            sum += (unsigned long)i;
        now(&w, &c);
        printf("主循环: wall=+%ldms cpu=+%ldms SIGXCPU 已命中 %d 次 sum=%lu\n", w - t0wall,
               c - t0cpu, (int)hits, (unsigned long)sum);
        fflush(stdout);
    }
    return 0; // 不可达:SIGKILL 先到
}
