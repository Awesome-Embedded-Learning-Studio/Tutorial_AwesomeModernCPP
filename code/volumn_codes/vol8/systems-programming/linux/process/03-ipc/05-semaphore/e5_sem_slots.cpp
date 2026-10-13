// E5a 计数信号量的「名额」时序:命名信号量 /lp03_sem 初值 2(两个名额),
// 三个互不相干的进程(exec 出来的 worker)按 150 ms 梯次进场抢名额——
// 前两个直接进,第三个干等,直到先到者 1 秒后 sem_post 放行。全程单调时钟对时。
// 与 Lmem03 的互斥锁竞态实验不同:互斥锁是「一把钥匙」,计数信号量是「N 个名额」。
// 用法:e5_sem_slots            → orchestrator
//       e5_sem_slots worker <rank> → worker
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e5_sem_slots.cpp -o e5_sem_slots -pthread
#include "ipc_util.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <semaphore.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
const char* kName = "/lp03_sem";

double boot_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1e6;
}
} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);

    if (argc >= 3 && std::strcmp(argv[1], "worker") == 0) {
        const int rank = std::atoi(argv[2]);
        sem_t* s = sem_open(kName, 0); // 名字已经存在,直接相认
        if (s == SEM_FAILED) {
            perror("worker sem_open");
            return 1;
        }
        usleep(rank * 150 * 1000); // 梯次进场:0/150/300 ms
        int v = -1;
        sem_getvalue(s, &v);
        std::printf("[worker%d pid=%d @%7.1f] 尝试 sem_wait(此刻 sem_getvalue=%d)\n", rank,
                    getpid(), boot_ms(), v);
        const double t0 = boot_ms();
        sem_wait(s); // 阻塞直到拿到名额
        sem_getvalue(s, &v);
        std::printf(
            "[worker%d pid=%d @%7.1f] 拿到名额!sem_wait 等了 %6.1f ms(剩余名额 sem_getvalue=%d)\n",
            rank, getpid(), boot_ms(), boot_ms() - t0, v);
        usleep(1000 * 1000); // 占用名额 1 秒
        sem_post(s);
        sem_getvalue(s, &v);
        std::printf("[worker%d pid=%d @%7.1f] sem_post 放行(名额回到 %d)\n", rank, getpid(),
                    boot_ms(), v);
        sem_close(s);
        return 0;
    }

    // ---- orchestrator ----
    sem_unlink(kName);
    sem_t* s = sem_open(kName, O_CREAT | O_EXCL, 0600, 2); // 两个名额
    if (s == SEM_FAILED) {
        perror("sem_open create");
        return 1;
    }
    int v = -1;
    sem_getvalue(s, &v);
    std::printf("命名信号量 %s 已建:初值=%d(两个名额)。三个 exec 出来的 worker 进场:\n", kName, v);

    pid_t pids[3];
    for (int i = 0; i < 3; ++i) {
        pids[i] = sys_call("fork", fork);
        if (pids[i] == 0) {
            char rank[8];
            std::snprintf(rank, sizeof rank, "%d", i);
            execl(argv[0], "e5_sem_slots", "worker", rank, (char*)nullptr);
            _exit(127);
        }
    }
    for (int i = 0; i < 3; ++i) {
        int st = 0;
        waitpid(pids[i], &st, 0);
    }
    sem_getvalue(s, &v);
    std::printf(
        "全部结束:sem_getvalue=%d(名额一个不少)。worker2 的「等了 xx ms」就是名额干等的时长\n", v);
    sem_close(s);
    sem_unlink(kName);
    return 0;
}
