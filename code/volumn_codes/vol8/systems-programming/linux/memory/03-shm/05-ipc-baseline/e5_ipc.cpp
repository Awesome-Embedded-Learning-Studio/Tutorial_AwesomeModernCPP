// E5:同一 1 MiB 负载的两种搬运途径吞吐对照
//   shm:共享内存 SPSC 环形队列(1024 B × 1024 条,槽 256),两进程
//   pipe:pipe 分 1024 次写 + 1024 次读,同样逐块校验
// 用法:e5_ipc <shm|pipe>(shell 循环 3 轮取中位)
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -pthread e5_ipc.cpp -o e5_ipc
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr const char* kName = "/lm03_e5";
constexpr uint32_t kChunks = 1024; // 1 MiB / 1024 B
constexpr uint32_t kChunkB = 1024;
constexpr uint32_t kSlots = 256; // 环形队列槽位(SPMC 之外同 E4 的 SPSC)
static_assert((kSlots & (kSlots - 1)) == 0);
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
    std::atomic<uint32_t> start;
    std::atomic<uint32_t> ready;
    uint64_t received;
    uint64_t corrupt;
    ProdLine p;
    ConsLine c;
};

inline uint64_t mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
inline void fill_chunk(uint64_t* buf, uint32_t idx) {
    for (uint32_t j = 0; j < kChunkB / 8; ++j)
        buf[j] = mix64(idx * 1000003ULL + j);
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

int run_shm() {
    const size_t total = sizeof(Header) + static_cast<size_t>(kSlots) * kChunkB;
    shm_unlink(kName);
    int fd = shm_open(kName, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        perror("shm_open");
        return 1;
    }
    ftruncate(fd, static_cast<off_t>(total));
    void* base = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    std::memset(base, 0, total);
    auto* h = static_cast<Header*>(base);
    auto* slots = static_cast<uint8_t*>(base) + sizeof(Header);

    pid_t pid = fork();
    if (pid == 0) {
        auto* ch = static_cast<Header*>(base);
        auto* cslots = slots;
        ch->ready.store(1, std::memory_order_release);
        while (ch->start.load(std::memory_order_acquire) == 0)
            cpu_relax();
        uint64_t corrupt = 0;
        uint64_t t = 0;
        std::vector<uint64_t> verify(kChunkB / 8);
        for (uint32_t expected = 0; expected < kChunks; ++expected) {
            while (t == ch->p.head.load(std::memory_order_acquire))
                cpu_relax();
            std::memcpy(verify.data(), cslots + (t & (kSlots - 1)) * kChunkB, kChunkB);
            for (uint32_t j = 0; j < kChunkB / 8; ++j) {
                if (verify[j] != mix64(expected * 1000003ULL + j))
                    ++corrupt;
            }
            ++t;
            ch->c.tail.store(t, std::memory_order_release);
        }
        ch->received = t;
        ch->corrupt = corrupt;
        _exit(0);
    }
    while (h->ready.load(std::memory_order_acquire) == 0)
        cpu_relax();
    const double t0 = now_ms();
    h->start.store(1, std::memory_order_release);
    uint64_t head = 0;
    for (uint32_t i = 0; i < kChunks; ++i) {
        while (head - h->c.tail.load(std::memory_order_acquire) >= kSlots)
            cpu_relax();
        fill_chunk(reinterpret_cast<uint64_t*>(slots + (head & (kSlots - 1)) * kChunkB), i);
        ++head;
        h->p.head.store(head, std::memory_order_release);
    }
    while (h->c.tail.load(std::memory_order_acquire) != kChunks)
        cpu_relax();
    const double t1 = now_ms();
    int st = 0;
    waitpid(pid, &st, 0);
    std::printf(
        "shm    1 MiB(1024 B × %u 块):received=%llu corrupt=%llu 耗时 %.3f ms → %.0f MiB/s\n",
        kChunks, static_cast<unsigned long long>(h->received),
        static_cast<unsigned long long>(h->corrupt), t1 - t0, 1.0 / (t1 - t0) * 1000.0);
    munmap(base, total);
    close(fd);
    shm_unlink(kName);
    return 0;
}

int run_pipe() {
    int data_pipe[2], ctl_pipe[2]; // ctl:子进程回报 就绪'R'/完成'D'
    if (pipe(data_pipe) != 0 || pipe(ctl_pipe) != 0) {
        perror("pipe");
        return 1;
    }
    pid_t pid = fork();
    if (pid == 0) {
        close(data_pipe[1]);
        close(ctl_pipe[0]);
        char c = 'R';
        if (write(ctl_pipe[1], &c, 1) != 1)
            _exit(1); // 就绪
        std::vector<uint64_t> buf(kChunkB / 8);
        uint64_t corrupt = 0;
        for (uint32_t i = 0; i < kChunks; ++i) {
            size_t got = 0;
            while (got < kChunkB) {
                ssize_t r =
                    read(data_pipe[0], reinterpret_cast<uint8_t*>(buf.data()) + got, kChunkB - got);
                if (r < 0) {
                    if (errno == EINTR)
                        continue;
                    perror("child read");
                    _exit(1);
                }
                if (r == 0)
                    _exit(1);
                got += static_cast<size_t>(r);
            }
            for (uint32_t j = 0; j < kChunkB / 8; ++j) {
                if (buf[j] != mix64(i * 1000003ULL + j))
                    ++corrupt;
            }
        }
        c = corrupt == 0 ? 'D' : 'X';
        if (write(ctl_pipe[1], &c, 1) != 1)
            _exit(1);
        _exit(0);
    }
    close(data_pipe[0]);
    close(ctl_pipe[1]);
    char c = 0;
    if (read(ctl_pipe[0], &c, 1) != 1) {
        perror("wait ready");
        return 1;
    }
    const double t0 = now_ms();
    std::vector<uint64_t> buf(kChunkB / 8);
    for (uint32_t i = 0; i < kChunks; ++i) {
        fill_chunk(buf.data(), i);
        size_t sent = 0;
        while (sent < kChunkB) {
            ssize_t w =
                write(data_pipe[1], reinterpret_cast<uint8_t*>(buf.data()) + sent, kChunkB - sent);
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                perror("write");
                return 1;
            }
            sent += static_cast<size_t>(w);
        }
    }
    c = 0;
    if (read(ctl_pipe[0], &c, 1) != 1 || c != 'D') {
        std::printf("pipe:子进程校验失败\n");
        return 1;
    }
    const double t1 = now_ms();
    int st = 0;
    waitpid(pid, &st, 0);
    std::printf("pipe   1 MiB(1024 B × %u 块):received=%u corrupt=0 耗时 %.3f ms → %.0f MiB/s\n",
                kChunks, kChunks, t1 - t0, 1.0 / (t1 - t0) * 1000.0);
    close(data_pipe[1]);
    close(ctl_pipe[0]);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "用法:e5_ipc <shm|pipe>\n");
        return 2;
    }
    if (std::strcmp(argv[1], "shm") == 0)
        return run_shm();
    if (std::strcmp(argv[1], "pipe") == 0)
        return run_pipe();
    std::fprintf(stderr, "未知模式 %s\n", argv[1]);
    return 2;
}
