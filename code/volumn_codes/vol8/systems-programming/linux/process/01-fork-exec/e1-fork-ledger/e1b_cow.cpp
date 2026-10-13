// E1b 写时复制(COW)实证:地址不变,物理分家
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -o e1b_cow e1b_cow.cpp
// 思路:fork 后父子看到同一个虚拟地址;谁都不写时,两边共享同一批物理页,
//      表现为两边 Pss 各约一半(共享页对半分账);子进程把 64 MiB 全写一遍后,
//      COW 触发整块物理复制,子进程 Pss 涨到全额——物理上分家了,地址却从没变过。
//      /proc/self/pagemap 看物理页帧号需要 root,这里用 smaps_rollup 的 Pss 代替,
//      Pss 的定义就是"把这些页真的全算我自己要付多少内存"。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

static long g_shared = 100; // 数据段里的全局变量

struct Rollup {
    long rss = 0, pss = 0, priv_clean = 0, priv_dirty = 0;
};

static Rollup read_rollup() {
    Rollup r;
    FILE* f = std::fopen("/proc/self/smaps_rollup", "r");
    if (!f) {
        std::perror("/proc/self/smaps_rollup");
        return r;
    }
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        const char* colon = std::strchr(line, ':');
        if (!colon)
            continue;
        long v = std::atol(colon + 1);
        if (std::strncmp(line, "Rss:", 4) == 0)
            r.rss = v;
        else if (std::strncmp(line, "Pss:", 4) == 0)
            r.pss = v;
        else if (std::strncmp(line, "Private_Clean:", 14) == 0)
            r.priv_clean = v;
        else if (std::strncmp(line, "Private_Dirty:", 14) == 0)
            r.priv_dirty = v;
    }
    std::fclose(f);
    return r;
}

static void print_rollup(const char* tag) {
    Rollup r = read_rollup();
    std::printf("%-12s Rss=%7ld kB  Pss=%7ld kB  Private_Clean=%7ld kB  Private_Dirty=%7ld kB\n",
                tag, r.rss, r.pss, r.priv_clean, r.priv_dirty);
    std::fflush(stdout);
}

int main() {
    const size_t kBytes = 64u << 20; // 64 MiB 匿名内存(足够压过进程基线)
    unsigned char* buf = new unsigned char[kBytes];
    for (size_t i = 0; i < kBytes; i += 4096)
        buf[i] = 1; // 逐页触碰,页才会真分配
    long sum = 0;
    for (size_t i = 0; i < kBytes; i += 4096)
        sum += buf[i];

    std::printf("全局变量 g_shared:地址=%p 初值=%ld\n", (void*)&g_shared, g_shared);
    std::printf("64 MiB 缓冲:地址=%p (逐页校验和=%ld)\n", (void*)buf, sum);
    print_rollup("fork 前:");

    int p2c[2];
    if (pipe(p2c)) {
        std::perror("pipe");
        return 1;
    }
    pid_t child = fork();

    if (child == 0) {
        close(p2c[1]);
        std::printf("子进程刚出生,同一批地址:g_shared 地址=%p 值=%ld\n", (void*)&g_shared,
                    g_shared);
        print_rollup("子·未写:");
        char b;
        if (read(p2c[0], &b, 1) != 1)
            _exit(3); // 等父进程把自己的共享快照打印完

        g_shared = 200; // 写数据段:COW 一次,值从此分家
        for (size_t i = 0; i < kBytes; i += 4096)
            buf[i] = 2; // 整块写:物理整块复制
        std::printf("子进程写完:g_shared 地址=%p 值=%ld(地址没变,值分家了)\n", (void*)&g_shared,
                    g_shared);
        print_rollup("子·写后:");
        std::fflush(stdout);
        _exit(0);
    }

    close(p2c[0]);
    print_rollup("父·fork后:"); // 此时子未写,物理页还共享着
    if (write(p2c[1], "g", 1) != 1) {
        std::perror("write");
        return 1;
    }
    close(p2c[1]);

    int st = 0;
    waitpid(child, &st, 0);
    std::printf("子进程退出后父进程再读:g_shared=%ld(没被子进程改成的 200 污染)\n", g_shared);
    print_rollup("父·子退后:");
    std::printf("解读:fork 后谁都没写时,父子 Pss 各约一半(共享页对半分账);\n");
    std::printf("      子进程把 64 MiB 写满后 Pss 变全额,两边 Pss 之和≈128 MiB——\n");
    std::printf("      物理内存复制了一份,而 %p 这个地址两边从头到尾没变过,这就是写时复制。\n",
                (void*)buf);
    return 0;
}
