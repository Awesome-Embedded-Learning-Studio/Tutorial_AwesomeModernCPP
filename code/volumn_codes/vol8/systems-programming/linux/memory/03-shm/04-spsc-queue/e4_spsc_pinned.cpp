// E4 附:钉核对照版(e4_spsc 的变体,父进程钉 CPU0、子进程钉 CPU1)
// 由来:未钉核时 yield 反比 spin 快 60%,怀疑 WSL2 调度把两进程放同核——钉核排除该变量
// 原始版见 e4_spsc.cpp
// E4 招牌:SPSC 无锁环形缓冲消息队列,放 shm_open 共享内存,两进程 100 万条消息
// 结构:head/tail 各占一条 cache line(防伪共享),槽区跟在头部后面
//   生产者:占 head,满则等(head-tail==容量)——背压;写槽,head 以 release 发布
//   消费者:占 tail,空则等(tail==head);读槽校验,tail 以 release 发布
// 顺序由 SPSC 结构保证:消费侧按 expected 序号核对,校验和防内容损坏
// 用法:e4_spsc <spin|yield> [条数=1000000]
//   spin:满/空时 CPU 自旋(x86 pause)
//   yield:满/空时 sched_yield() 让出(队列满时的背压版)
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -pthread e4_spsc.cpp -o e4_spsc
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

constexpr const char* kName = "/lm03_spsc";
constexpr uint32_t kSlots = 1024;  // 2 的幂,掩码取模
constexpr uint32_t kMsgBytes = 32; // 每条消息 32 B
static_assert((kSlots & (kSlots - 1)) == 0);

// lock-free(address-free)原子操作跨进程才成立——静态断言钉死
static_assert(std::atomic<uint64_t>::is_always_lock_free);

struct alignas(64) ProdLine {
    std::atomic<uint64_t> head;
    char pad[64 - sizeof(std::atomic<uint64_t>)];
};
struct alignas(64) ConsLine {
    std::atomic<uint64_t> tail;
    char pad[64 - sizeof(std::atomic<uint64_t>)];
};

struct Header {
    std::atomic<uint32_t> start; // 起跑线
    std::atomic<uint32_t> ready; // 消费者就绪
    uint64_t nmsgs;
    uint32_t slot_bytes;
    uint32_t slots;
    // 消费者填,父进程在 waitpid 后读
    uint64_t received;
    uint64_t order_err;
    uint64_t corrupt;
    int32_t consumer_pid;
    ProdLine p;
    ConsLine c;
};
struct Msg {
    uint64_t seq;
    uint64_t checksum;
    uint64_t payload[2];
};

inline uint64_t mix64(uint64_t x) { // splitmix64 终结器,当消息校验和
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

double now_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1e6;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "用法:e4_spsc <spin|yield> [N]\n");
        return 2;
    }
    const bool yield_mode = std::strcmp(argv[1], "yield") == 0;
    const uint64_t n = argc >= 3 ? std::strtoull(argv[2], nullptr, 10) : 1'000'000;

    const size_t total = sizeof(Header) + static_cast<size_t>(kSlots) * kMsgBytes;
    shm_unlink(kName);
    int fd = shm_open(kName, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        perror("shm_open");
        return 1;
    }
    if (ftruncate(fd, static_cast<off_t>(total)) != 0) {
        perror("ftruncate");
        return 1;
    }
    void* base = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    std::memset(base, 0, total); // 零值对 lock-free atomic 是合法初值

    auto* h = static_cast<Header*>(base);
    auto* slots = reinterpret_cast<Msg*>(static_cast<char*>(base) + sizeof(Header));
    h->nmsgs = n;
    h->slot_bytes = kMsgBytes;
    h->slots = kSlots;

    const auto backoff = [yield_mode] {
        if (yield_mode)
            sched_yield();
        else
            cpu_relax();
    };

    pid_t pid = fork();
    if (pid == 0) { // ---- 消费者:子进程,按名字重新打开同一实体 ----
        {           // 钉核对照:子进程固定到 CPU1(父进程钉 CPU0,见 fork 之后)
            cpu_set_t set;
            CPU_ZERO(&set);
            CPU_SET(1, &set);
            if (sched_setaffinity(0, sizeof set, &set) != 0)
                perror("child sched_setaffinity");
        }
        int cfd = shm_open(kName, O_RDWR, 0);
        if (cfd < 0) {
            perror("child shm_open");
            _exit(1);
        }
        auto* ch =
            static_cast<Header*>(mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, cfd, 0));
        if (ch == MAP_FAILED) {
            perror("child mmap");
            _exit(1);
        }
        auto* cslots = reinterpret_cast<Msg*>(reinterpret_cast<char*>(ch) + sizeof(Header));
        ch->consumer_pid = static_cast<int32_t>(getpid());
        ch->ready.store(1, std::memory_order_release);
        while (ch->start.load(std::memory_order_acquire) == 0)
            cpu_relax();

        uint64_t got_order_err = 0, got_corrupt = 0, got = 0;
        uint64_t t = 0;
        for (uint64_t expected = 0; expected < ch->nmsgs; ++expected) {
            while (t == ch->p.head.load(std::memory_order_acquire))
                backoff(); // 空
            const Msg& m = cslots[t & (kSlots - 1)];
            if (m.seq != expected)
                ++got_order_err;
            if (m.checksum != mix64(m.seq))
                ++got_corrupt;
            ++got;
            ++t;
            ch->c.tail.store(t, std::memory_order_release);
        }
        ch->received = got;
        ch->order_err = got_order_err;
        ch->corrupt = got_corrupt;
        _exit(0);
    }

    // ---- 生产者:父进程 ----
    { // 钉核对照:父进程固定到 CPU0
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(0, &set);
        if (sched_setaffinity(0, sizeof set, &set) != 0)
            perror("parent sched_setaffinity");
    }
    while (h->ready.load(std::memory_order_acquire) == 0)
        cpu_relax();
    const double t0 = now_ms();
    h->start.store(1, std::memory_order_release);

    uint64_t head = 0;
    for (uint64_t seq = 0; seq < n; ++seq) {
        while (head - h->c.tail.load(std::memory_order_acquire) >= kSlots)
            backoff(); // 满 → 背压
        Msg& m = slots[head & (kSlots - 1)];
        m.seq = seq;
        m.checksum = mix64(seq);
        m.payload[0] = seq ^ 0xa5a5a5a5a5a5a5a5ULL;
        m.payload[1] = mix64(seq ^ 0xdeadbeefULL);
        ++head;
        h->p.head.store(head, std::memory_order_release);
    }
    while (h->c.tail.load(std::memory_order_acquire) != n)
        cpu_relax(); // 等消费者消费完
    const double t1 = now_ms();
    int st = 0;
    waitpid(pid, &st, 0);

    const double sec = (t1 - t0) / 1000.0;
    std::printf("%-5s N=%llu 槽=%u×%uB(共 %zuB 实体):", argv[1], static_cast<unsigned long long>(n),
                kSlots, kMsgBytes, total);
    std::printf("received=%llu order_err=%llu corrupt=%llu(子进程 pid=%d)",
                static_cast<unsigned long long>(h->received),
                static_cast<unsigned long long>(h->order_err),
                static_cast<unsigned long long>(h->corrupt), h->consumer_pid);
    std::printf(" 耗时 %.1f ms,%.2f 百万条/秒,有效载荷 %.1f MiB/s\n", t1 - t0,
                static_cast<double>(n) / sec / 1e6,
                static_cast<double>(n) * kMsgBytes / sec / 1024.0 / 1024.0);

    munmap(base, total);
    close(fd);
    shm_unlink(kName);
    return 0;
}
