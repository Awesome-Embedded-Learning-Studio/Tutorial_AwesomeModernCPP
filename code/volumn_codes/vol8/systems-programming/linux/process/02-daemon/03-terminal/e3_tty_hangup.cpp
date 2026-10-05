// E3 有控制终端的进程组:关掉 pty master(= 终端死亡) -> 内核对会话发 SIGHUP
//   链路:父(旁观者,自己的会话没有这个终端) -> 子(setsid+TIOCSCTTY,会话长,不装
//   handler,吃缺省动作) -> 两个组员(捕获 SIGHUP 看时间戳)
//   trace 走专用 fd3:子进程的 stdio 会被 dup2 到 slave 上,不能再靠 stdout
// 编译: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o e3_tty_hangup e3_tty_hangup.cpp -lutil
// 运行: ./e3_tty_hangup e3_tty_hangup.out && cat e3_tty_hangup.out
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static int tfd = -1;
static long t0 = 0;

static long now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// async-signal-safe 手工排版,只 write 不 printf
static void hup_handler(int) {
    char buf[112];
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
    ssize_t wr = write(tfd, buf, n);
    (void)wr;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "e3_tty_hangup.out";
    tfd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (tfd < 0) {
        perror("open trace");
        return 1;
    }
    t0 = now_ms();

    int mfd = -1, sfd = -1;
    if (openpty(&mfd, &sfd, nullptr, nullptr, nullptr) != 0) {
        perror("openpty");
        return 1;
    }
    int ready[2];
    if (pipe(ready) != 0) {
        perror("pipe");
        return 1;
    }

    dprintf(tfd, "== E3 终端死亡 -> SIGHUP ==\n");
    dprintf(tfd, "父 pid=%d(旁观者)开了一对 pty:master=%d slave=%d\n\n", (int)getpid(), mfd, sfd);

    pid_t leader = fork();
    if (leader == 0) {
        setsid();                 // 新会话长
        ioctl(sfd, TIOCSCTTY, 0); // slave 成为我的控制终端
        dup2(sfd, STDIN_FILENO);
        dup2(sfd, STDOUT_FILENO);
        dup2(sfd, STDERR_FILENO);
        if (sfd > STDERR_FILENO)
            close(sfd);
        close(mfd); // 会话长这侧不需要 master

        pid_t fg = getpgid(0);
        tcsetpgrp(STDIN_FILENO, fg); // 我的组设为前台组(吃键盘中断/SIGHUP 的组)

        dprintf(tfd, "会话长 pid=%d: 控制终端=%s, sid=%d pgid=%d(前台组),\n", (int)getpid(),
                ttyname(STDIN_FILENO), (int)getsid(0), (int)fg);
        dprintf(tfd, "  它不装 SIGHUP handler,缺省动作 = 终止\n");

        for (int i = 0; i < 2; ++i) {
            if (fork() == 0) { // 组员:继承组长 pgid,装 handler 观察时序
                struct sigaction sa{};
                sa.sa_handler = hup_handler;
                sigemptyset(&sa.sa_mask);
                sigaction(SIGHUP, &sa, nullptr);
                sigset_t blk;
                sigemptyset(&blk);
                sigaddset(&blk, SIGHUP);
                sigprocmask(SIG_BLOCK, &blk, nullptr);
                char b = 'r';
                ssize_t wr = write(ready[1], &b, 1);
                (void)wr;
                sigset_t empty;
                sigemptyset(&empty);
                sigsuspend(&empty); // 原子放行+等待,消除就绪/等待间的窗口
                dprintf(tfd, "组员 pid=%d: 捕获 SIGHUP 后主动退出(缺省动作本该把我终止)\n",
                        (int)getpid());
                _exit(0);
            }
        }
        char b = 'r';
        ssize_t wr = write(ready[1], &b, 1);
        (void)wr;
        pause();  // 等死:SIGHUP 缺省动作处决我
        _exit(0); // 不可达
    }
    close(sfd);

    for (int i = 0; i < 3; ++i) {
        char b;
        ssize_t rd = read(ready[0], &b, 1);
        (void)rd;
    }
    usleep(150000);
    dprintf(tfd, "\n父: 一切就绪,close(master=%d) —— 模拟\"终端先死\"\n", mfd);
    close(mfd);

    int st = 0;
    waitpid(leader, &st, 0); // 会话长应该被 SIGHUP 处决
    usleep(500000);          // 等组员把 trace 写完

    if (WIFSIGNALED(st))
        dprintf(tfd, "\n父: waitpid(会话长) -> 被 %s 处决(缺省动作如约而至)\n",
                strsignal(WTERMSIG(st)));
    else
        dprintf(tfd, "\n父: waitpid(会话长) -> st=0x%x(异常:竟然不是被信号杀的)\n", st);
    dprintf(tfd, "结论: 终端一死,前台组人人有份 SIGHUP——\n");
    dprintf(tfd, "这就是守护进程第一步就 setsid 脱离终端的原因:不做,进程的命就拴在终端上\n");
    return 0;
}
