// linux_spsc.cpp —— E4 的 Linux 同机对照:shm_open + eventfd 的 SPSC 环形队列,100 万条
//
// 编译(WSL2 本机 g++,与 Windows 侧 e4_spsc_ring 同一台物理机、同一套结构):
//   g++ -std=c++20 -O2 -Wall -Wextra linux_spsc.cpp -o linux_spsc
// 运行(与 Windows 侧同名的四种模式):
//   ./linux_spsc spin|hybrid|notify|burst
//
// 结构刻意与 Windows 侧一比一:
//   Ring 头(tail/head 各占一行缓存行)+ 4096 槽 × 16B{value,tag},同样逐条核对;
//   通知通道把命名事件换成 eventfd(auto-reset 事件 ≈ 非 SEMAPHORE 模式的 eventfd:
//   write 加计数、read 清零,醒来后能取多少取多少);双进程用 fork,同样钉 cpu2/cpu3
// 观察点:
//   (1) 100 万条零丢失零乱序(值+tag 逐条验)
//   (2) 三模式吞吐与 Windows 侧同机直接比:两边 syscall 形态不同(SetEvent vs
//       eventfd write/read),但"逐条通知比自旋慢一个数量级"的形状应当一致
//   (3) WSL2 是跑在同一颗 9700X 上的虚拟机,内核/调度不同,绝对数字各记各侧,
//       相对关系才是可比的那一层

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <immintrin.h>
#include <sched.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static constexpr uint64_t kMsgCount = 1'000'000;
static constexpr uint64_t kCap = 4096;
static constexpr size_t kShmSize = 0x20000;

struct alignas(64) RingHead {
    uint64_t magic;
    uint64_t nmsgs;
    uint64_t capacity;
    uint64_t mask;
    std::atomic<uint64_t> tail;
    char pad0[64 - sizeof(std::atomic<uint64_t>)];
    std::atomic<uint64_t> head;
    char pad1[64 - sizeof(std::atomic<uint64_t>)];
    std::atomic<uint64_t> sleeping;
    std::atomic<uint64_t> go;    // fork 后的起跑闸(自旋等待,基准里可忽略)
    std::atomic<uint64_t> cdone; // 消费者收工标志
    uint64_t cons_ns;
    uint64_t cons_errors;
    uint64_t cons_sleeps;
};
struct Slot {
    uint64_t value;
    uint64_t tag;
};

static uint64_t mix(uint64_t v) {
    v *= 0x9E3779B97F4A7C15ull;
    return v ^ (v >> 31);
}

static int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static void pin_cpu(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    sched_setaffinity(0, sizeof set, &set);
}

int main(int argc, char** argv) {
    const char* mode = argc >= 2 ? argv[1] : "hybrid";
    const bool m_notify = strcmp(mode, "notify") == 0;
    const bool m_spin = strcmp(mode, "spin") == 0;
    const bool m_burst = strcmp(mode, "burst") == 0;
    if (!m_notify && !m_spin && !m_burst && strcmp(mode, "hybrid") != 0) {
        printf("用法:./linux_spsc spin|hybrid|notify|burst\n");
        return 1;
    }
    char name[64];
    snprintf(name, sizeof name, "/sysprog_spsc_%d", (int)getpid());
    int fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd < 0) {
        perror("shm_open");
        return 1;
    }
    ftruncate(fd, (off_t)kShmSize);
    auto* rh = (RingHead*)mmap(nullptr, kShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    // tmpfs 新文件本就是零页,原子量零值合法,直接填业务字段
    rh->magic = 0x53505343ull;
    rh->nmsgs = kMsgCount;
    rh->capacity = kCap;
    rh->mask = kCap - 1;
    Slot* slots = (Slot*)((unsigned char*)rh + 0x1000);
    int efd = eventfd(0, 0); // 非 SEMAPHORE:read 清零计数,醒来后能取多少取多少

    pid_t cpid = fork();
    if (cpid == 0) { // ---- 消费者(子进程),钉 cpu3 ----
        pin_cpu(3);
        uint64_t expected = 0, errors = 0, sleeps = 0, h = 0;
        while (rh->go.load(std::memory_order_acquire) == 0) {
            _mm_pause();
        }
        int64_t t0 = now_ns();
        while (expected < rh->nmsgs) {
            uint64_t t = rh->tail.load(std::memory_order_acquire);
            if (h == t) {
                if (m_spin) {
                    _mm_pause();
                    continue;
                }
                if (m_notify) {
                    uint64_t one = 0;
                    read(efd, &one, 8);
                    ++sleeps;
                    continue;
                }
                for (int i = 0; i < 20000 && h == t; ++i) {
                    _mm_pause();
                    t = rh->tail.load(std::memory_order_acquire);
                }
                if (h == t) {
                    rh->sleeping.store(1, std::memory_order_seq_cst);
                    t = rh->tail.load(std::memory_order_acquire);
                    if (h == t) {
                        uint64_t one = 0;
                        read(efd, &one, 8);
                        ++sleeps;
                    }
                    rh->sleeping.store(0, std::memory_order_seq_cst);
                }
                continue;
            }
            do {
                Slot& s = slots[h & rh->mask];
                if (s.value != expected || s.tag != mix(s.value)) {
                    ++errors;
                }
                ++expected;
                h = h + 1;
                rh->head.store(h, std::memory_order_release);
                t = rh->tail.load(std::memory_order_acquire);
            } while (h != t && expected < rh->nmsgs);
        }
        rh->cons_ns = (uint64_t)(now_ns() - t0);
        rh->cons_errors = errors;
        rh->cons_sleeps = sleeps;
        rh->cdone.store(1, std::memory_order_release);
        _exit(errors == 0 ? 0 : 2);
    }

    // ---- 生产者(父进程),钉 cpu2 ----
    pin_cpu(2);
    printf("[E4-Linux] 模式=%s 消息数=%llu 槽数=4096(16B/槽) 对象=%s 通知=eventfd\n", mode,
           (unsigned long long)kMsgCount, name);
    uint64_t t_cache = 0, full_spins = 0, notifies = 0, tail = 0;
    uint64_t one = 1;
    rh->go.store(1, std::memory_order_release);
    int64_t t0 = now_ns();
    for (uint64_t i = 0; i < kMsgCount; ++i) {
        while (tail - t_cache == kCap) {
            t_cache = rh->head.load(std::memory_order_acquire);
            ++full_spins;
            _mm_pause();
        }
        if (m_burst && i % 10000 == 0 && i > 0) {
            usleep(5000);
        }
        Slot& s = slots[tail & rh->mask];
        s.value = i;
        s.tag = mix(i);
        tail = tail + 1;
        rh->tail.store(tail, std::memory_order_release);
        if (m_notify) {
            write(efd, &one, 8);
            ++notifies;
        } else if (!m_spin) {
            std::atomic_thread_fence(std::memory_order_seq_cst);
            if (rh->sleeping.load(std::memory_order_relaxed)) {
                write(efd, &one, 8);
                ++notifies;
            }
        }
    }
    int64_t prod_ns = now_ns() - t0;
    while (rh->cdone.load(std::memory_order_acquire) == 0) {
        _mm_pause();
    }
    int wstat = 0;
    waitpid(cpid, &wstat, 0);
    int cexit = WIFEXITED(wstat) ? WEXITSTATUS(wstat) : -1;

    printf("[生产者 pid=%d cpu2] %llu 条,%.2f ms,吞吐 %.0f 万条/s(%.2f ns/条),遇满自旋 %llu 次\n",
           (int)getpid(), (unsigned long long)kMsgCount, prod_ns / 1e6,
           (double)kMsgCount / (double)prod_ns * 1e9 / 1e4, prod_ns / (double)kMsgCount,
           (unsigned long long)full_spins);
    printf("[消费者 退出码=%d] %llu 条,核对错误 %llu 条(值+tag 逐条验),%.2f ms,吞吐 %.0f 万条/s,%s "
           "%llu 次\n",
           cexit, (unsigned long long)kMsgCount, (unsigned long long)rh->cons_errors,
           (double)rh->cons_ns / 1e6, (double)kMsgCount / (double)rh->cons_ns * 1e9 / 1e4,
           m_notify ? "等待" : "睡眠", (unsigned long long)rh->cons_sleeps);
    printf("[E4-Linux] %s:通知共 %llu 次;%s\n", mode, (unsigned long long)notifies,
           rh->cons_errors == 0 ? "100 万条零丢失零乱序" : "!! 有差错,查协议");
    munmap(rh, kShmSize);
    close(fd);
    shm_unlink(name);
    close(efd);
    return 0;
}
