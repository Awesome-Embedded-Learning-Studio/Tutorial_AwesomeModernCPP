// E2: canonical 行缓冲 vs raw 逐键——读端看到的分块与时机
// 台架:父进程 openpty,按阶段先配置 slave 的 termios,再 fork 孩子把 slave 接到 fd0;
//      父进程按时刻表往 master 喂字节,孩子每完成一次 read 就把【开始/完成时刻/字节数/内容】
//      写上报告管道,读满 K 次就退。父进程收齐报告、收尸,再把 master 里的回显抽干一并上报。
//      全程只动这只自建 pty,主终端一个字节不碰。
#include <fcntl.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/wait.h>
#include <utility>
#include <vector>

static double now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

struct Report {
    double t_start, t_end;
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
        else if (c == 0x7f)
            s += "DEL";
        else if (c == 0x15)
            s += "NAK";
        else if (c < 32) {
            std::snprintf(b, sizeof b, "^%c", c + '@');
            s += b;
        } else
            s += (char)c;
    }
    s += ")";
    return s;
}

struct FeedItem {
    int delay_ms;
    const char* bytes;
};

static void run_phase(const char* name, const termios* cfg, int K,
                      const std::vector<FeedItem>& feed) {
    int mfd, sfd;
    openpty(&mfd, &sfd, nullptr, nullptr, nullptr);
    if (cfg)
        tcsetattr(sfd, TCSANOW, cfg);

    int rp[2];
    pipe(rp);
    double t0 = now_ms();
    pid_t pid = fork();
    if (pid == 0) { // 孩子:slave 接 fd0,读 K 次上报
        close(mfd);
        close(rp[0]);
        dup2(sfd, 0);
        close(sfd);
        for (int i = 0; i < K; ++i) {
            Report r{};
            r.t_start = now_ms() - t0;
            r.n = read(0, r.buf, (size_t)K >= sizeof r.buf ? sizeof r.buf : 256);
            r.t_end = now_ms() - t0;
            if (write(rp[1], &r, sizeof r) < 0)
                break;
            if (r.n <= 0)
                break;
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
    while (read(rp[0], &r, sizeof r) == (ssize_t)sizeof r) {
        if (r.n < 0)
            std::printf("  读 t=[%7.1f..%7.1f]ms n=%d(read 报错:%s)\n", r.t_start, r.t_end, r.n,
                        std::strerror(errno));
        else
            std::printf("  读 t=[%7.1f..%7.1f]ms n=%-3d %s\n", r.t_start, r.t_end, r.n,
                        hexof(r.buf, r.n).c_str());
    }
    waitpid(pid, nullptr, 0);
    close(rp[0]);

    // 抽干 master:收的是 tty 层的回显(E2 的主角是读端,回显只点名,E5 展开机制)
    fcntl(mfd, F_SETFL, O_NONBLOCK);
    usleep(50 * 1000);
    std::string echo;
    char b[256];
    ssize_t n;
    while ((n = read(mfd, b, sizeof b)) > 0)
        echo += hexof((const unsigned char*)b, (int)n);
    std::printf("  master 收到的回显:%s\n\n", echo.empty() ? "(无)" : echo.c_str());
    close(mfd);
}

int main() {
    std::printf(
        "E2:canonical 行缓冲 vs raw 逐键(时刻表:喂 300ms 一次,读端什么时候拿到、拿到几块)\n\n");

    termios canon{}; // 出厂即 canonical,这里显式取一份留档
    {
        int m, s;
        openpty(&m, &s, nullptr, nullptr, nullptr);
        tcgetattr(s, &canon);
        close(m);
        close(s);
    }

    run_phase("阶段1 canonical:行没写完,read 一直干等", &canon, 1,
              {{0, "ab"}, {300, "cd"}, {300, "\n"}});

    run_phase("阶段2 canonical:DEL(0x7f) 的行内编辑发生在 tty 层,读端拿不到", &canon, 1,
              {{0, "abc"}, {200, "\x7f"}, {200, "d\n"}});

    run_phase("阶段3 canonical:NAK(^U) 整行抹掉,读端只看到新行", &canon, 1,
              {{0, "abc"}, {200, "\x15"}, {200, "xy\n"}});

    termios raw = canon;
    cfmakeraw(&raw);
    run_phase("阶段4 raw(cfmakeraw,VMIN=1):喂几块到几块,没有行概念", &raw, 3,
              {{0, "ab"}, {300, "cd"}, {300, "\n"}});
    return 0;
}
