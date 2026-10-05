// E1b 进程组广播:kill(-pgid, SIGHUP) 一次送达组内所有成员,组外进程毫发无损
//   组长(setpgid(0,0)) + 两个组员(继承组长 pgid),父进程在组外开火
//   真实世界"终端退出 -> 内核对前台组 kill(-pgid, SIGHUP)"的全真版见 E3 的 pty 实验
// 编译: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o e1_pgrp_sighup e1_pgrp_sighup.cpp
// 运行: ./e1_pgrp_sighup
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static long now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static long t0 = 0;

// 信号处理器里只能用 async-signal-safe 调用:write + 手工排版
static void hup_handler(int) {
    char buf[96];
    size_t n = 0;
    auto put = [&](const char* s) {
        while (*s)
            buf[n++] = *s++;
    };
    auto putnum = [&](long v) {
        char tmp[24];
        int i = 0;
        do {
            tmp[i++] = char('0' + v % 10);
            v /= 10;
        } while (v);
        while (i)
            buf[n++] = tmp[--i];
    };
    put("[SIGHUP] pid=");
    putnum(getpid());
    put(" t=+");
    putnum(now_ms() - t0);
    put("ms\n");
    ssize_t wr = write(STDOUT_FILENO, buf, n);
    (void)wr;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0); // 子进程 _exit 不刷 stdio 缓冲,先关缓冲再 fork
    t0 = now_ms();
    struct sigaction sa{};
    sa.sa_handler = hup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, nullptr);

    int ready[2];
    if (pipe(ready) != 0) {
        perror("pipe");
        return 1;
    }

    printf("== E1b kill(-pgid, SIGHUP): 进程组整体接收 ==\n");
    printf("父进程 pid=%d(自己的组是另一个,开火后不应收到)\n\n", (int)getpid());
    fflush(stdout);

    pid_t leader = fork();
    if (leader == 0) {
        if (setpgid(0, 0) != 0)
            perror("setpgid"); // 组长:pgid == pid
        for (int i = 0; i < 2; ++i) {
            if (fork() == 0) { // 组员:不 setpgid,继承组长 pgid
                // 先挡住 SIGHUP 再报就绪,消除"报完就绪、还没睡下"的窗口
                sigset_t blk;
                sigemptyset(&blk);
                sigaddset(&blk, SIGHUP);
                sigprocmask(SIG_BLOCK, &blk, nullptr);
                char b = 'r';
                ssize_t wr = write(ready[1], &b, 1);
                (void)wr;
                sigset_t empty;
                sigemptyset(&empty);
                sigsuspend(&empty); // 原子地放行信号并等待
                printf("组员 pid=%d: 捕获 SIGHUP 后主动退出(缺省动作本该把我直接终止)\n",
                       (int)getpid());
                _exit(0);
            }
        }
        char b = 'r';
        ssize_t wr = write(ready[1], &b, 1);
        (void)wr;
        pause();
        for (int i = 0; i < 2; ++i)
            wait(nullptr);
        _exit(0);
    }

    // 父进程:收齐 3 个就绪字节再开火
    for (int i = 0; i < 3; ++i) {
        char b;
        ssize_t rd = read(ready[0], &b, 1);
        (void)rd;
    }
    usleep(100000);
    printf("父进程: kill(%d, SIGHUP) —— 负数 pid = 定向整个进程组 %d\n", (int)-leader, (int)leader);
    fflush(stdout);
    kill(-leader, SIGHUP);

    usleep(300000);
    printf("\n父进程: 枪响 300ms 后我什么都没收到(上面没有我的 pid)—— 组外不受影响\n");

    int st = 0;
    waitpid(leader, &st, 0);
    printf("组长(pid=%d, pgid=%d) 退出: 捕获后主动退出,退出码 %d\n", (int)leader, (int)leader,
           WEXITSTATUS(st));
    printf("结论: kill(-pgid, sig) 是按\"组\"投递的;终端退出时内核对前台组发的正是这种投递\n");
    return 0;
}
