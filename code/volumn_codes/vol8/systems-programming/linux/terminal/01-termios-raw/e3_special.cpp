// E3:特殊字符与 ISIG——^C 在 canonical 是信号,在 raw 是字节;^D 的 EOF 不粘
// 台架同 E2,孩子在读循环外加 SIGINT/SIGQUIT 计数器,每份报告带上计数。
// 关注四件事:
//   1) ^C(VINTR):信号送达 + 输入队列被冲(NOFLSH 未设,排队中的 "ab" 没了,读端只看到之后的行)
//   2) ^D(VEOF):canonical 下把没换行的行立刻交出去;再一个 ^D 让 read 返回 0(EOF);
//      但 tty 的 EOF 不粘——后续还能接着读(和管道/文件不同)
//   3) ^\(VQUIT):SIGQUIT 同样走信号路
//   4) raw(cfmakeraw) 下 0x03/0x04/0x1c 全是普通字节,计数器纹丝不动
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/wait.h>
#include <vector>

static double now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static volatile sig_atomic_t g_int = 0, g_quit = 0;
static void on_int(int) {
    g_int = g_int + 1;
}
static void on_quit(int) {
    g_quit = g_quit + 1;
}

struct Report {
    double t_start, t_end;
    int n;
    int sigint, sigquit;
    unsigned char buf[256];
};

static std::string hexof(const unsigned char* p, int n) {
    std::string s;
    char b[8];
    for (int i = 0; i < n; ++i) {
        std::snprintf(b, sizeof b, "%02x ", p[i]);
        s += b;
    }
    s += "(";
    for (int i = 0; i < n; ++i) {
        unsigned char c = p[i];
        if (c == '\n')
            s += "\\n";
        else if (c == 0x03)
            s += "^C";
        else if (c == 0x04)
            s += "^D";
        else if (c == 0x1c)
            s += "^\\";
        else if (c == 0x7f)
            s += "DEL";
        else if (c < 32) {
            std::snprintf(b, sizeof b, "^%c", c + '@');
            s += b;
        } else
            s += (char)c;
    }
    return s + ")";
}

struct FeedItem {
    int delay_ms;
    const char* bytes;
};

static void run_phase(const char* name, const termios* cfg, int K, bool foreground,
                      const std::vector<FeedItem>& feed) {
    int mfd, sfd;
    openpty(&mfd, &sfd, nullptr, nullptr, nullptr);
    if (cfg)
        tcsetattr(sfd, TCSANOW, cfg);

    int rp[2];
    pipe(rp);
    double t0 = now_ms();
    pid_t pid = fork();
    if (pid == 0) {
        close(mfd);
        close(rp[0]);
        dup2(sfd, 0);
        close(sfd);
        if (foreground) {
            // 让自己成为这只 pty 的前台进程组:setsid 脱离原会话、认 slave 为控制终端、
            // 再把自己组设为前台组。^C 的信号只发给前台组,不迈这三步就没人接。
            setsid();
            ioctl(0, TIOCSCTTY, 0);
            tcsetpgrp(0, getpgrp());
        }
        signal(SIGINT, on_int);
        signal(SIGQUIT, on_quit);
        for (int i = 0; i < K; ++i) {
            Report r{};
            r.t_start = now_ms() - t0;
            r.n = read(0, r.buf, 256);
            r.t_end = now_ms() - t0;
            r.sigint = (int)g_int;
            r.sigquit = (int)g_quit;
            if (write(rp[1], &r, sizeof r) < 0)
                break;
            if (r.n <= 0 && i + 1 < K)
                continue; // tty 的 EOF 不粘,继续读
        }
        close(rp[1]);
        _exit(0);
    }
    close(rp[1]);
    close(sfd);

    std::printf("[%s]\n", name);
    int elapsed = 0;
    for (const auto& f : feed) {
        usleep(f.delay_ms * 1000);
        elapsed += f.delay_ms;
        size_t len = std::strlen(f.bytes);
        write(mfd, f.bytes, len);
        std::printf("  喂 t=%4dms %s\n", elapsed,
                    hexof((const unsigned char*)f.bytes, (int)len).c_str());
    }

    Report r;
    while (read(rp[0], &r, sizeof r) == (ssize_t)sizeof r)
        std::printf("  读 t=[%7.1f..%7.1f]ms n=%-3d %s 计数:SIGINT=%d SIGQUIT=%d\n", r.t_start,
                    r.t_end, r.n, hexof(r.buf, r.n).c_str(), r.sigint, r.sigquit);
    int st = 0;
    waitpid(pid, &st, 0);
    fcntl(mfd, F_SETFL, O_NONBLOCK);
    usleep(50 * 1000);
    std::string echo;
    char b[256];
    ssize_t n;
    while ((n = read(mfd, b, sizeof b)) > 0)
        echo += hexof((const unsigned char*)b, (int)n);
    std::printf("  master 收到的回显:%s   孩子退出码=%d\n\n", echo.empty() ? "(无)" : echo.c_str(),
                WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    close(mfd);
}

int main() {
    std::printf("E3:特殊字符与 ISIG——同一串字节,两种模式下走两条完全不同的路\n\n");

    termios canon{};
    {
        int m, s;
        openpty(&m, &s, nullptr, nullptr, nullptr);
        tcgetattr(s, &canon);
        close(m);
        close(s);
    }

    run_phase("阶段1 canonical 无会话:^C 冲掉排队的 ab,但信号没有收件人(SIGINT=0)", &canon, 1,
              false, {{0, "ab"}, {200, "\x03"}, {200, "cd\n"}});

    run_phase("阶段1b canonical 前台组:setsid+TIOCSCTTY+tcsetpgrp 三步之后,同一场 ^C 有人接了",
              &canon, 1, true, {{0, "ab"}, {200, "\x03"}, {200, "cd\n"}});

    run_phase("阶段2 canonical:^D 交出行、再 ^D 是 EOF,EOF 之后还能接着读", &canon, 3, false,
              {{0, "ab"}, {200, "\x04"}, {200, "\x04"}, {200, "x\n"}});

    run_phase("阶段3 canonical 前台组:^\\ 是 SIGQUIT", &canon, 1, true,
              {{0, "ab"}, {200, "\x1c"}, {200, "cd\n"}});

    termios raw = canon;
    cfmakeraw(&raw);
    run_phase("阶段4 raw:0x03/0x04/0x1c 全是普通字节,一次 read 原样全收", &raw, 1, false,
              {{0, "ab\x03"
                   "cd\x04\x1c\n"}});
    return 0;
}
