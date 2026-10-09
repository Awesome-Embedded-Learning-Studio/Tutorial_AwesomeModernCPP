// E4:VMIN/VTIME 四组合——非 canonical 模式下 read 的返回条件实测
// VTIME 单位是 0.1 秒。四组合 + 两个追加判定:
//   A  MIN=1 TIME=0 :来一个字节就能读走一个(阻塞到至少 1 字节)
//   B  MIN=4 TIME=0 :凑够 4 字节才返回,喂得再散也一次拿 4
//   C  MIN=0 TIME=5 :计时器读——没数据 0.5 秒后返回 0(EOF 长相);有数据立刻拿走,不凑数
//   D  MIN=4 TIME=5 :字符间计时器的语义判定——每 200ms 喂 1 字节(间隔<0.5s):
//                     若计时器每收一字节重置 → 第 4 字节到时凑满返回 4;
//                     若只从第 1 字节起算 → 0.5s 到点时只有 3 字节,返回 3。让输出裁决。
//   E  MIN=4 TIME=5 :只喂 1 字节然后断供 → 计时器到点返回已有的 1 字节(不会死等 MIN)
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
    for (int i = 0; i < n; ++i)
        s += (char)p[i];
    return s + ")";
}

struct FeedItem {
    int delay_ms;
    const char* bytes;
};

static void run_phase(const char* name, unsigned char vmin, unsigned char vtime, int K,
                      const std::vector<FeedItem>& feed) {
    int mfd, sfd;
    openpty(&mfd, &sfd, nullptr, nullptr, nullptr);
    termios raw{};
    tcgetattr(sfd, &raw);
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = vmin;
    raw.c_cc[VTIME] = vtime;
    tcsetattr(sfd, TCSANOW, &raw);

    int rp[2];
    pipe(rp);
    double t0 = now_ms();
    pid_t pid = fork();
    if (pid == 0) {
        close(mfd);
        close(rp[0]);
        dup2(sfd, 0);
        close(sfd);
        for (int i = 0; i < K; ++i) {
            Report r{};
            r.t_start = now_ms() - t0;
            r.n = read(0, r.buf, 256);
            r.t_end = now_ms() - t0;
            if (write(rp[1], &r, sizeof r) < 0)
                break;
            if (r.n <= 0 && i + 1 < K)
                continue; // MIN=0 计时到点返回 0,继续下一轮
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
        std::printf("  读 t=[%7.1f..%7.1f]ms n=%-2d %s\n", r.t_start, r.t_end, r.n,
                    r.n == 0 ? "(计时到点,EOF 长相)" : hexof(r.buf, r.n).c_str());
    waitpid(pid, nullptr, 0);
    close(rp[0]);
    close(mfd);
    std::printf("\n");
}

int main() {
    std::printf("E4:VMIN/VTIME 组合(cfmakeraw 基底,只动 VMIN/VTIME 两格)\n\n");

    run_phase("A: MIN=1 TIME=0 —— 来一个给一个", 1, 0, 3, {{0, "x"}, {300, "y"}, {300, "z"}});

    run_phase("B: MIN=4 TIME=0 —— 凑够 4 才交货,散喂也一次拿 4(两轮各 4 字节)", 4, 0, 2,
              {{0, "a"},
               {250, "b"},
               {250, "c"},
               {250, "d"},
               {250, "e"},
               {250, "f"},
               {250, "g"},
               {250, "h"}});

    run_phase("C: MIN=0 TIME=5 —— 没数据 0.5s 返回 0,有数据立刻拿走", 0, 5, 3,
              {{0, ""}, {600, "ab"}, {600, ""}});

    run_phase("D: MIN=4 TIME=5 —— 每 200ms 喂 1 字节,计时器重不重置,看返回", 4, 5, 1,
              {{0, "a"}, {200, "b"}, {200, "c"}, {200, "d"}});

    run_phase("E: MIN=4 TIME=5 —— 只喂 1 字节后断供,计时到点交出 1 字节", 4, 5, 1, {{0, "q"}});

    run_phase("F: MIN=4 TIME=5 —— a@0ms、b@450ms 后断供:计时器从第 2 字节重起则 ~950ms 交 2 字节;"
              "只从第 1 字节起算则 ~500ms 交 1 字节",
              4, 5, 1, {{0, "a"}, {450, "b"}});
    return 0;
}
