// E5b 信号量杂项四连:sem_timedwait 超时(ETIMEDOUT)、sem_getvalue 观察、
// sem_init(pshared=1) 匿名信号量跨 fork 生效、命名信号量的 unlink 生命周期
// (旧句柄照用、新名字从头来——与 Lmem03 E1 的 shm_unlink 语义同构,那里已细讲,这里只对账)。
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e5_sem_misc.cpp -o e5_sem_misc -pthread
#include "ipc_util.hpp"

#include <cstdio>
#include <cstring>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
double boot_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1e6;
}
int val(sem_t* s) {
    int v = -1;
    sem_getvalue(s, &v);
    return v;
}
} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);

    // ---- 1. sem_timedwait 超时 ----
    std::printf("== 1. sem_timedwait:300 ms 超时 ==\n");
    sem_t* s = sem_open("/lp03_misc", O_CREAT | O_EXCL, 0600, 0); // 0 个名额
    struct timespec dl{};
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_nsec += 300 * 1000 * 1000;
    if (dl.tv_nsec >= 1000000000L) {
        dl.tv_sec += 1;
        dl.tv_nsec -= 1000000000L;
    }
    const double t0 = boot_ms();
    errno = 0;
    int r = sem_timedwait(s, &dl);
    std::printf("sem_timedwait(300ms) 返回 %d,errno=%d (%s),实测等了 %.1f ms\n", r, errno,
                std::strerror(errno), boot_ms() - t0);

    // ---- 2. sem_getvalue ----
    std::printf("\n== 2. sem_getvalue 观察 post 前后 ==\n");
    sem_post(s);
    std::printf("post 一次 → %d", val(s));
    sem_post(s);
    std::printf(";再 post → %d", val(s));
    sem_wait(s);
    std::printf(";wait 一次 → %d\n", val(s));

    // ---- 3. sem_init(pshared=1) 匿名:跨 fork 生效 ----
    std::printf("\n== 3. sem_init(pshared=1) 匿名信号量:放进共享匿名映射,fork 后两个进程共用 ==\n");
    sem_t* shared = static_cast<sem_t*>(
        mmap(nullptr, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    if (shared == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    if (sem_init(shared, 1, 1) != 0) {
        perror("sem_init");
        return 1;
    } // 1 个名额
    pid_t pid = sys_call("fork", fork);
    if (pid == 0) {
        const double tc = boot_ms();
        sem_wait(shared);
        std::printf("  [子进程 @%7.1f] 匿名 sem_wait 拿到(等了 %.1f ms)——父子共用同一个计数器\n",
                    boot_ms(), boot_ms() - tc);
        usleep(400 * 1000);
        sem_post(shared);
        std::printf("  [子进程 @%7.1f] 匿名 sem_post 放行\n", boot_ms());
        _exit(0);
    }
    usleep(100 * 1000); // 让子进程先拿走唯一的名额
    const double tp = boot_ms();
    sem_wait(shared); // 父进程得等子进程放行
    std::printf("  [父进程 @%7.1f] 匿名 sem_wait 拿到(等了 %.1f ms,等的就是子进程那次 post)\n",
                boot_ms(), boot_ms() - tp);
    sem_post(shared);
    int st = 0;
    waitpid(pid, &st, 0);
    sem_destroy(shared);
    munmap(shared, sizeof(sem_t));
    std::printf("  对照:sem_init 的 pshared=0 版本只在本进程内有效,跨 fork 的语义靠 pshared=1 + "
                "共享内存\n");

    // ---- 4. 命名信号量 unlink 生命周期 ----
    std::printf("\n== 4. unlink 生命周期:旧句柄照用,新名字从头来(与 shm_unlink 同构) ==\n");
    std::printf("unlink 前旧句柄值=%d;执行 sem_unlink……\n", val(s));
    sem_unlink("/lp03_misc");
    sem_t* fresh = sem_open("/lp03_misc", O_CREAT | O_EXCL, 0600, 7); // 同名重建,新实体
    if (fresh == SEM_FAILED) {
        perror("recreate");
        return 1;
    }
    sem_post(s); // 操作的还是旧实体
    std::printf("旧句柄 post 后=%d;新句柄(初值 7)=%d——两个实体互不相干,名字已经归新实体\n", val(s),
                val(fresh));
    sem_close(s);
    sem_close(fresh);
    sem_unlink("/lp03_misc");
    return 0;
}
