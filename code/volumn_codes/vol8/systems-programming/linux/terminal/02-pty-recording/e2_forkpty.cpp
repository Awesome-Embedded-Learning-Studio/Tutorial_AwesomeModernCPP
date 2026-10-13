// E2:forkpty 一条龙——一个调用替你做了几件事,逐项验收
// forkpty 干的事:开 pty 对(内部=posix_openpt 四步)、fork、孩子侧 setsid+把 slave 认作
// 控制终端(login_tty 的活)、slave 接到 0/1/2。验收单:
//   孩子侧:isatty(0/1/2)、ttyname、pid==sid(会话长)、ioctl(0,TIOCGSID) 成功(有控制终端)
//   通路:父写 master "ping\n",孩子 read(0) 收到;孩子 write(1) "pong",父读 master 收到
//   对照:手搓路径(setsid+TIOCSCTTY+tcsetpgrp)在上一篇 E3 阶段1b 摆过、daemon 篇 E3 也用过,
//         本篇只对表不重跑
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

static double now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static std::string hexof(const char* p, size_t n) {
    std::string s = "(";
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)p[i];
        if (c == '\n')
            s += "\\n";
        else if (c == '\r')
            s += "\\r";
        else
            s += (char)c;
    }
    return s + ")";
}

int main() {
    // 报告管道必须在 forkpty 之前就位,fd 号随内存继承给孩子
    int rp[2];
    pipe(rp);

    int mfd = -1;
    pid_t pid = forkpty(&mfd, nullptr, nullptr, nullptr);
    if (pid < 0) {
        perror("forkpty");
        return 1;
    }
    if (pid == 0) {
        close(rp[0]);
        close(mfd); // 孩子不碰 master
        char msg[512];
        int sid = 0;
        int gsid = ioctl(0, TIOCGSID, &sid);
        int fg = 0;
        int gfg = ioctl(0, TIOCGPGRP, &fg);
        int len = std::snprintf(msg, sizeof msg,
                                "孩子报告: pid=%d sid=%d(pid==sid:%d,会话长) pgid=%d "
                                "isatty0/1/2=%d/%d/%d ttyname(0)=%s "
                                "TIOCGSID=%d(sid=%d,控制终端到手) TIOCGPGRP=%d(前台组=%d,%s)\n",
                                (int)getpid(), (int)getsid(0), (int)(getpid() == getsid(0)),
                                (int)getpgid(0), isatty(0), isatty(1), isatty(2), ttyname(0), gsid,
                                sid, gfg, fg, fg == (int)getpgid(0) ? "自己" : "别人");
        (void)write(rp[1], msg, (size_t)len);
        // 通路:读一行 stdin(经 tty),回一行 stdout(经 tty)
        char buf[128];
        read(0, buf, sizeof buf);
        write(1, "pong\n", 5);
        close(rp[1]);
        _exit(0);
    }
    close(rp[1]);

    usleep(50 * 1000);
    char msg[512];
    ssize_t rn = read(rp[0], msg, sizeof msg - 1);
    if (rn > 0) {
        msg[rn] = 0;
        std::printf("%s", msg);
    }

    double t0 = now_ms();
    write(mfd, "ping\n", 5);
    std::string got;
    char b[256];
    // 读 master:孩子的回显 + pong 都从这里出
    while (got.find("pong") == std::string::npos) {
        ssize_t n = read(mfd, b, sizeof b);
        if (n <= 0)
            break;
        got.append(b, (size_t)n);
    }
    std::printf("父进程 t=%.1fms 收齐 master 流:%s —— 里面混着孩子的回显与输出,一条流分不出方向\n",
                now_ms() - t0, hexof(got.data(), got.size()).c_str());

    int st = 0;
    waitpid(pid, &st, 0);
    std::printf("孩子退出:%d\n", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    close(rp[0]);
    close(mfd);
    return 0;
}
