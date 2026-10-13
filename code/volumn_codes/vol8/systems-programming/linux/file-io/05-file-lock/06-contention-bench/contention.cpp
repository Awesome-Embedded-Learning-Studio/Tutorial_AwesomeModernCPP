// e6_contention.cpp —— 锁竞争计时(文章《文件锁》E6)
//
// 口径:N=8 个子进程(fork),各自独立 open,管道对齐起跑线:
//   locked:每轮 flock(LOCK_EX) → usleep(10ms) → flock(LOCK_UN),100 轮
//   free  :同样 8×100×usleep(10ms),只是不拿锁(对照:纯并行睡眠)
//   micro :8 进程 × 2000 轮「空临界区」lock/unlock,量纯锁传递开销
// 每种模式跑 3 轮取中位数。locked 的预期:临界区被串行化,总时长 ≈
// 8 × 100 × 10 ms = 8 s;free 的预期 ≈ 1 s。
#include "article.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

const char* path = "/home/charliechen/l05_scratch/e6/contention.bin";

constexpr int kWorkers = 8;
constexpr int kRounds = 100;
constexpr int kMicroRounds = 2000;

double now_ms() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// 一次测量:fork kWorkers 个孩子,管道放行,全员跑完返回墙钟毫秒
double measure(bool use_lock, bool micro) {
    int go[2];
    sys_call("pipe", ::pipe, go);
    std::vector<pid_t> kids;
    for (int i = 0; i < kWorkers; ++i) {
        pid_t pid = ::fork();
        if (pid == 0) {
            ::close(go[1]);
            char c = 0;
            if (::read(go[0], &c, 1) != 1) {
                ::_exit(1);
            }
            unique_fd own{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
            const int rounds = micro ? kMicroRounds : kRounds;
            for (int r = 0; r < rounds; ++r) {
                if (use_lock) {
                    sys_call("flock", ::flock, own.get(), LOCK_EX);
                }
                if (!micro) {
                    ::usleep(10 * 1000); // 临界区:睡 10 ms
                }
                if (use_lock) {
                    sys_call("flock", ::flock, own.get(), LOCK_UN);
                }
            }
            ::_exit(0);
        }
        kids.push_back(pid);
    }
    ::close(go[0]);
    const double t1 = now_ms();
    for (int i = 0; i < kWorkers; ++i) {
        char c = 'g';
        sys_call("write", ::write, go[1], &c, 1);
    }
    for (pid_t pid : kids) {
        int st = 0;
        ::waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            std::printf("!! worker %d 异常 st=%#x\n", pid, st);
        }
    }
    ::close(go[1]);
    return now_ms() - t1;
}

void report(const char* tag, std::vector<double> v, int rounds, int workers) {
    std::sort(v.begin(), v.end());
    const double med = v[v.size() / 2];
    std::printf("%-8s", tag);
    for (double x : v) {
        std::printf("  %8.1f ms", x);
    }
    std::printf("  | 中位 %8.1f ms", med);
    if (rounds > 0) {
        const double total_cs = med;                         // 串行总时长
        const double one_cs = total_cs / (workers * rounds); // 摊到每次临界区
        std::printf(",%.1f µs/人次", one_cs * 1000.0);
    }
    std::printf("\n");
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    { // 清场
        unique_fd fd{sys_call("open", ::open, path, O_RDWR | O_CREAT | O_TRUNC, 0666)};
    }
    std::printf("pid=%d, 文件:%s(ext4), %d 工作进程 × %d 轮,临界区 usleep(10ms),3 轮取中位\n\n",
                ::getpid(), path, kWorkers, kRounds);

    std::printf("模式        r1            r2            r3\n");
    report("locked", {measure(true, false), measure(true, false), measure(true, false)}, kRounds,
           kWorkers);
    report("free", {measure(false, false), measure(false, false), measure(false, false)}, kRounds,
           kWorkers);

    std::printf("\n空临界区(纯锁传递开销):%d 进程 × %d 轮 lock/unlock\n", kWorkers, kMicroRounds);
    report("micro", {measure(true, true), measure(true, true), measure(true, true)}, 0, 0);
    return 0;
}
