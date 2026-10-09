// E5:ECHO 与 OPOST——tty 是双向过滤器,输入与输出各自过各自的门
//   阶段1  ECHO 开:父进程喂 "hi\n",master 里收到回显 "hi\r\n"(连回显都要过 OPOST 的 ONLCR);
//          ECHO 关:再喂 "secret\n",master 里空空如也,读端照收——密码不回显的机制
//   阶段2  OPOST 开:孩子往 slave 写 "A\nB",master 收到 "A\r\nB"(\n 被翻成 \r\n);
//          OPOST 关:同样的 "A\nB",master 收到原样的 "A\nB"
//   阶段3  ICRNL 开:喂 "x\r",读端收到 "x\n"(CR 被翻成 NL);
//          ICRNL 关:喂 "y\r",读端收到原样的 "y\r"
#include <fcntl.h>
#include <pty.h>
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

struct Report {
    double t_ms;
    int n;
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
        else if (c == '\r')
            s += "\\r";
        else
            s += (char)c;
    }
    return s + ")";
}

static std::string drain_master(int mfd, const char* tag) {
    usleep(80 * 1000);
    fcntl(mfd, F_SETFL, O_NONBLOCK);
    std::string total;
    char b[256];
    ssize_t n;
    while ((n = read(mfd, b, sizeof b)) > 0)
        total += hexof((const unsigned char*)b, (int)n);
    fcntl(mfd, F_SETFL, 0);
    std::printf("    master 侧(%s):%s\n", tag, total.empty() ? "(无)" : total.c_str());
    return total;
}

int main() {
    // ---------- 阶段1:ECHO 开/关 ----------
    {
        int mfd, sfd;
        openpty(&mfd, &sfd, nullptr, nullptr, nullptr);
        int rp[2];
        pipe(rp);
        pid_t pid = fork();
        if (pid == 0) { // 读端:收 2 行
            close(mfd);
            close(rp[0]);
            dup2(sfd, 0);
            close(sfd);
            for (int i = 0; i < 2; ++i) {
                Report r{};
                r.t_ms = now_ms();
                r.n = read(0, r.buf, 256);
                write(rp[1], &r, sizeof r);
                if (r.n <= 0)
                    break;
            }
            close(rp[1]);
            _exit(0);
        }
        close(rp[1]);
        close(sfd);
        std::printf("[阶段1 ECHO:回显是 tty 干的,关掉它,读端照收、屏幕无痕]\n");

        write(mfd, "hi\n", 3);
        std::printf("  喂(ECHO 开) t=0ms hi\\n\n");
        drain_master(mfd, "回显");

        termios t{};
        tcgetattr(mfd, &t);
        t.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(mfd, TCSANOW, &t); // 经 master 端设置,E1 已证两端同步
        usleep(50 * 1000);
        write(mfd, "secret\n", 7);
        std::printf("  喂(ECHO 关) t=150ms secret\\n\n");
        drain_master(mfd, "回显");

        Report r;
        while (read(rp[0], &r, sizeof r) == (ssize_t)sizeof r)
            std::printf("    读端收到 n=%d %s\n", r.n, hexof(r.buf, r.n).c_str());
        waitpid(pid, nullptr, 0);
        close(rp[0]);
        close(mfd);
        std::printf("\n");
    }

    // ---------- 阶段2:OPOST 开/关 ----------
    {
        int mfd, sfd;
        openpty(&mfd, &sfd, nullptr, nullptr, nullptr);
        pid_t pid = fork();
        if (pid == 0) { // 写端:写两轮 "A\nB",中间留 400ms 给父进程切 OPOST
            close(mfd);
            dup2(sfd, 1);
            close(sfd);
            write(1, "A\nB", 3);
            usleep(400 * 1000);
            write(1, "A\nB", 3);
            usleep(100 * 1000);
            _exit(0);
        }
        close(sfd);
        std::printf("[阶段2 OPOST:孩子两次写同样的 A\\nB,门里门外差一个 \\r]\n");
        drain_master(mfd, "OPOST 开,第一轮");
        termios t{};
        tcgetattr(mfd, &t);
        t.c_oflag &= ~(tcflag_t)OPOST;
        tcsetattr(mfd, TCSANOW, &t);
        std::printf("    (t=200ms 清 OPOST)\n");
        usleep(400 * 1000);
        drain_master(mfd, "OPOST 关,第二轮");
        waitpid(pid, nullptr, 0);
        close(mfd);
        std::printf("\n");
    }

    // ---------- 阶段3:ICRNL 开/关 ----------
    {
        int mfd, sfd;
        openpty(&mfd, &sfd, nullptr, nullptr, nullptr);
        termios base{};
        tcgetattr(sfd, &base);
        termios noicr = base;
        noicr.c_iflag &= ~(tcflag_t)ICRNL;
        tcsetattr(sfd, TCSANOW, &noicr); // 先关,后面再开
        int rp[2];
        pipe(rp);
        pid_t pid = fork();
        if (pid == 0) { // 读端 2 次
            close(mfd);
            close(rp[0]);
            dup2(sfd, 0);
            close(sfd);
            for (int i = 0; i < 2; ++i) {
                Report r{};
                r.t_ms = now_ms();
                r.n = read(0, r.buf, 256);
                write(rp[1], &r, sizeof r);
                if (r.n <= 0)
                    break;
            }
            close(rp[1]);
            _exit(0);
        }
        close(rp[1]);
        close(sfd);
        std::printf("[阶段3 ICRNL:进门方向,CR 要不要被翻成 NL]\n");
        usleep(50 * 1000);
        // ICRNL 关着:CR 不是行结束符,补一个 ^D(VEOF) 把行交出去,读端才能收到
        write(mfd, "y\r\x04", 3);
        std::printf("  喂(ICRNL 关) y\\r ^D\n");
        usleep(50 * 1000);
        // 恢复 ICRNL 再喂:这回 CR 进门就变 NL,自己就是行结束符
        termios t{};
        tcgetattr(mfd, &t);
        t.c_iflag |= ICRNL;
        tcsetattr(mfd, TCSANOW, &t);
        usleep(50 * 1000);
        write(mfd, "x\r", 2);
        std::printf("  喂(ICRNL 开) x\\r\n");
        Report r;
        while (read(rp[0], &r, sizeof r) == (ssize_t)sizeof r)
            std::printf("    读端收到 n=%d %s\n", r.n, hexof(r.buf, r.n).c_str());
        waitpid(pid, nullptr, 0);
        close(rp[0]);
        close(mfd);
    }
    return 0;
}
