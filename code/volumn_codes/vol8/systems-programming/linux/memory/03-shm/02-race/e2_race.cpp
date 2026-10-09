// E2:跨进程读写与竞态——同一共享计数器,两进程各加 1,000,000 次
// ①nosync:无同步,非原子 ++,实测丢失更新(结果 < 2,000,000)
// ②mutex:PTHREAD_PROCESS_SHARED 互斥锁放在共享内存头部,结果精确 2,000,000
// ③sem:sem_init(pshared=1) 进程间信号量,对照
// 底层用 MAP_SHARED|MAP_ANONYMOUS(fork 前映射,子进程继承)——E3 专门对照两条途径
// 用法:e2_race <nosync|mutex|sem> [每人次数,默认 1000000]
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -pthread e2_race.cpp -o e2_race
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

struct Shared {
    pthread_mutex_t mtx;
    sem_t sem;
    volatile unsigned long long counter; // volatile:挡住编译器把 load/add/hoist 出循环
    std::atomic<int> ready;              // 子进程就绪计数
    std::atomic<int> start;              // 起跑线,计时口径统一
};

double now_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1e6;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "用法:e2_race <nosync|mutex|sem> [N]\n");
        return 2;
    }
    const char* mode = argv[1];
    const long long n = argc >= 3 ? std::strtoll(argv[2], nullptr, 10) : 1'000'000;

    void* mem =
        mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    std::memset(mem, 0, sizeof(Shared));
    auto* sh = new (mem) Shared{}; // 零初始化的 lock-free atomic 合法

    if (std::strcmp(mode, "mutex") == 0) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED); // 关键一步
        pthread_mutex_init(&sh->mtx, &attr);
        pthread_mutexattr_destroy(&attr);
    } else if (std::strcmp(mode, "sem") == 0) {
        if (sem_init(&sh->sem, 1, 1) != 0) {
            perror("sem_init");
            return 1;
        } // pshared=1
    } else if (std::strcmp(mode, "nosync") != 0) {
        std::fprintf(stderr, "未知模式 %s\n", mode);
        return 2;
    }

    for (int child = 0; child < 2; ++child) {
        pid_t pid = fork();
        if (pid == 0) {
            sh->ready.fetch_add(1, std::memory_order_acq_rel);
            while (sh->start.load(std::memory_order_acquire) == 0) { /* 自旋等起跑 */
            }
            for (long long i = 0; i < n; ++i) {
                if (mode[0] == 'm') { // mutex
                    pthread_mutex_lock(&sh->mtx);
                    sh->counter = sh->counter + 1;
                    pthread_mutex_unlock(&sh->mtx);
                } else if (mode[0] == 's') { // sem
                    while (sem_wait(&sh->sem) == -1 && errno == EINTR) {
                    }
                    sh->counter = sh->counter + 1;
                    sem_post(&sh->sem);
                } else { // nosync:裸加
                    sh->counter = sh->counter + 1;
                }
            }
            _exit(0);
        }
    }
    while (sh->ready.load(std::memory_order_acquire) < 2) { /* 等两个子进程就位 */
    }
    const double t0 = now_ms();
    sh->start.store(1, std::memory_order_release);
    int status = 0;
    waitpid(-1, &status, 0);
    waitpid(-1, &status, 0);
    const double t1 = now_ms();

    const unsigned long long expected = static_cast<unsigned long long>(n) * 2;
    const unsigned long long got = sh->counter;
    std::printf("%-7s 每人 %lld 次:结果 %llu / 期望 %llu", mode, n, got, expected);
    if (got == expected) {
        std::printf("(无丢失)");
    } else {
        std::printf("(丢失 %llu 次,%.2f%%)", expected - got,
                    100.0 * static_cast<double>(expected - got) / static_cast<double>(expected));
    }
    std::printf(" 耗时 %.1f ms(%.0f 万次/秒)\n", t1 - t0,
                static_cast<double>(expected) / (t1 - t0) / 10000.0);
    return 0;
}
