// E5:回放——按 timing 时刻表把 typescript 重新演一遍
// 用法:./e5_player session.timing session.typescript [倍速]
// 每笔:睡够距上一笔的间隔(可乘倍速),再把这笔字节写 stdout。
// 验收:全程墙钟与录制时长对表;回放产物与 typescript 逐字节比对(cmp)。
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>

static double now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void sleep_ms(double ms) {
    timespec ts{};
    ts.tv_sec = (time_t)(ms / 1000.0);
    ts.tv_nsec = (long)((ms - ts.tv_sec * 1000.0) * 1e6);
    nanosleep(&ts, nullptr);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "用法:%s timing typescript [倍速]\n", argv[0]);
        return 2;
    }
    double speed = argc > 3 ? std::atof(argv[3]) : 1.0;

    FILE* timing = fopen(argv[1], "rt");
    FILE* script = fopen(argv[2], "rb");
    if (!timing || !script) {
        perror("fopen");
        return 1;
    }

    double planned = 0, slept_total = 0;
    size_t chunks = 0, bytes = 0;
    char line[128];
    unsigned char buf[65536];
    double t0 = now_ms();
    while (fgets(line, sizeof line, timing)) {
        double delta = 0;
        size_t n = 0;
        if (std::sscanf(line, "%lf %zu", &delta, &n) != 2)
            continue;
        double want = delta / speed;
        double before = now_ms();
        sleep_ms(want);
        slept_total += now_ms() - before;
        planned += want;
        if (fread(buf, 1, n, script) != n) {
            std::fprintf(stderr, "typescript 短了一截\n");
            break;
        }
        ssize_t w = write(1, buf, n);
        if (w < 0) {
            perror("write");
            break;
        }
        bytes += (size_t)w;
        ++chunks;
    }
    double wall = now_ms() - t0;
    std::fprintf(stderr,
                 "回放完成:%zu 笔 %zu 字节,计划睡眠合计 %.1fms,实际睡眠合计 %.1fms,"
                 "全程墙钟 %.1fms(含写 stdout 的开销),倍速=%.2f\n",
                 chunks, bytes, planned, slept_total, wall, speed);
    fclose(timing);
    fclose(script);
    return 0;
}
