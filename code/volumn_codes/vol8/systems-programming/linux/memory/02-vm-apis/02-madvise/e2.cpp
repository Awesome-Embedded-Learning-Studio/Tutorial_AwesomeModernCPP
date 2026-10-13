// E2: madvise 全家 —— 四组实验
//   (A) MADV_NORMAL/SEQUENTIAL/RANDOM 对 readahead 的影响:ext4 上 64 MiB 文件,
//       fadvise(DONTNEED) 压干净页缓存后摸头 1 MiB,mincore 数驻留页数差;
//       MADV_WILLNEED 的异步预读直接用 mincore 前后对比观察;
//   (B) MADV_DONTNEED 匿名页:写入 -> DONTNEED -> 读回零页 + smaps Rss 掉一半;
//   (C) MADV_DONTFORK:fork 后子进程里该映射整段消失,访问报 SEGV_MAPERR
//       (对照:同 fork 的普通映射走 COW,子进程照常读写);
//   (D) MADV_REMOVE:私有匿名 -> EINVAL;ext4 文件映射 -> EINVAL;
//       /dev/shm(tmpfs)共享映射 -> 真打洞:中间页归零、文件长度不变、占块减少。
// 触错动作(C)沿用 fork 子进程 + handler 只用 write 的纪律。
#include "article.hpp"
#include "sigout.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr std::size_t kPage = 4096;
constexpr const char* kRaPath = "/home/charliechen/lm02_scratch/ra.bin"; // ext4 真盘
constexpr std::size_t kRaMiB = 64;

// ---- mincore 小工具:返回驻留页数 ----
std::size_t resident_pages(void* p, std::size_t bytes, unsigned char* vec) {
    const std::size_t npages = (bytes + kPage - 1) / kPage;
    if (::mincore(p, npages * kPage, vec) != 0) {
        std::printf("mincore failed errno=%d\n", errno);
        return static_cast<std::size_t>(-1);
    }
    std::size_t n = 0;
    for (std::size_t i = 0; i < npages; ++i) {
        n += (vec[i] & 1) != 0;
    }
    return n;
}

void print_bitmap(const unsigned char* vec, std::size_t npages, const char* label) {
    std::printf("   mincore %-12s ", label);
    for (std::size_t i = 0; i < npages; ++i) {
        std::printf("%c", (vec[i] & 1) ? '1' : '0');
    }
    std::printf("\n");
}

// ---- /proc/self/smaps:取包含 p 的 VMA 的某个字段(kB) ----
long smaps_field_kb(const void* p, const char* key) {
    std::FILE* f = std::fopen("/proc/self/smaps", "r");
    if (!f) {
        return -1;
    }
    char line[512];
    bool in_vma = false;
    long val = -1;
    const auto target = reinterpret_cast<std::uintptr_t>(p);
    while (std::fgets(line, sizeof line, f)) {
        std::uintptr_t s = 0, e = 0;
        if (std::sscanf(line, "%lx-%lx", &s, &e) == 2 && e > s && line[0] != ' ') {
            in_vma = target >= s && target < e;
            continue;
        }
        if (in_vma) {
            long v = 0;
            if (std::sscanf(line, "%*s %ld", &v) == 1) {
                char name[64] = {};
                std::sscanf(line, "%63[a-zA-Z_]:", name);
                if (std::strcmp(name, key) == 0) {
                    val = v;
                    break;
                }
            }
        }
    }
    std::fclose(f);
    return val;
}

// ---- (A) 准备 ext4 大文件 ----
int prepare_ra_file() {
    const int fd = ::open(kRaPath, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        std::printf("open %s failed errno=%d\n", kRaPath, errno);
        return -1;
    }
    struct stat st{};
    ::fstat(fd, &st);
    if (static_cast<std::size_t>(st.st_size) == kRaMiB * 1024 * 1024) {
        return fd;
    }
    if (::ftruncate(fd, static_cast<off_t>(kRaMiB) * 1024 * 1024) != 0) {
        std::printf("ftruncate failed errno=%d\n", errno);
        ::close(fd);
        return -1;
    }
    // 每页写一个非零字节,避免稀疏全零文件被特殊对待
    void* p = ::mmap(nullptr, kRaMiB * 1024 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        std::printf("mmap ra failed\n");
        ::close(fd);
        return -1;
    }
    auto* base = static_cast<unsigned char*>(p);
    for (std::size_t i = 0; i < kRaMiB * 256; ++i) {
        base[i * kPage] = static_cast<unsigned char>(i);
    }
    ::munmap(p, kRaMiB * 1024 * 1024);
    ::fdatasync(fd);
    return fd;
}

void evict_page_cache(int fd) {
    // POSIX_FADV_DONTNEED:把该文件的干净页从页缓存里请出去(无需 root)
    if (::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) != 0) {
        std::printf("fadvise failed errno=%d\n", errno);
    }
}

void part_a_readahead() {
    std::printf("\n== (A) MADV_NORMAL/SEQUENTIAL/RANDOM x readahead, via mincore ==\n");
    const int fd = prepare_ra_file();
    if (fd < 0) {
        return;
    }
    const std::size_t kBytes = kRaMiB * 1024 * 1024;
    const std::size_t kTouchPages = 256; // 1 MiB,每页摸 1 字节
    std::vector<unsigned char> vec(kBytes / kPage);

    const struct {
        int adv;
        const char* name;
    } modes[] = {{MADV_NORMAL, "NORMAL"}, {MADV_SEQUENTIAL, "SEQUENTIAL"}, {MADV_RANDOM, "RANDOM"}};
    for (const auto& m : modes) {
        evict_page_cache(fd);
        void* p = ::mmap(nullptr, kBytes, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) {
            std::printf("mmap failed\n");
            return;
        }
        std::size_t base = resident_pages(p, kBytes, vec.data());
        if (::madvise(p, kBytes, m.adv) != 0) {
            std::printf("madvise(%s) failed errno=%d\n", m.name, errno);
        }
        volatile unsigned char sink = 0;
        auto* q = static_cast<unsigned char*>(p);
        for (std::size_t i = 0; i < kTouchPages; ++i) {
            sink = static_cast<unsigned char>(sink + q[i * kPage]);
        }
        const std::size_t res = resident_pages(p, kBytes, vec.data());
        std::printf("   %-10s baseline %4zu KiB resident -> after touching 1 MiB: %6zu KiB"
                    " (+%4zu KiB beyond touched)\n",
                    m.name, base * 4, res * 4, res * 4 >= 1024 ? res * 4 - 1024 : 0);
        (void)sink;
        ::munmap(p, kBytes);
    }

    // MADV_WILLNEED:显式预读 8 MiB,不摸任何页,只看驻留
    std::printf("   MADV_WILLNEED (8 MiB, no touch):\n");
    evict_page_cache(fd);
    void* p = ::mmap(nullptr, kBytes, PROT_READ, MAP_PRIVATE, fd, 0);
    std::size_t before = resident_pages(p, kBytes, vec.data());
    std::printf("   before        : %4zu KiB resident\n", before * 4);
    if (::madvise(p, 8 * 1024 * 1024, MADV_WILLNEED) != 0) {
        std::printf("madvise WILLNEED failed errno=%d\n", errno);
    }
    std::size_t after = 0;
    for (int poll = 0; poll < 50; ++poll) { // 异步预读,轮询到稳定
        ::usleep(50 * 1000);
        const std::size_t now = resident_pages(p, kBytes, vec.data());
        if (now == after && after > before) {
            break;
        }
        after = now;
    }
    std::printf("   after WILLNEED: %4zu KiB resident (prefetch is async; polled until stable)\n",
                after * 4);
    ::munmap(p, kBytes);
    ::close(fd);
}

// ---- (B) MADV_DONTNEED 匿名页 ----
void part_b_dontneed_anon() {
    std::printf("\n== (B) MADV_DONTNEED on anonymous pages ==\n");
    constexpr std::size_t kBytes = 8 * 1024 * 1024;
    auto* p = static_cast<unsigned int*>(
        ::mmap(nullptr, kBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    for (std::size_t i = 0; i < kBytes / 4; ++i) {
        p[i] = 0xC0DE0000u + static_cast<unsigned>(i);
    }
    std::printf("   wrote 8 MiB pattern (0xC0DE0000+i), Rss = %ld kB\n", smaps_field_kb(p, "Rss"));
    std::printf("   p[0]=0x%08x, p[1MiB]=0x%08x, p[5MiB]=0x%08x\n", p[0], p[1024 * 1024 / 4],
                p[5 * 1024 * 1024 / 4]);

    if (::madvise(p, kBytes / 2, MADV_DONTNEED) != 0) {
        std::printf("madvise DONTNEED failed errno=%d\n", errno);
        return;
    }
    std::printf("   madvise(first 4 MiB, MADV_DONTNEED) rc=0, Rss = %ld kB\n",
                smaps_field_kb(p, "Rss"));
    std::printf("   read back: p[0]=0x%08x (zero page), p[1MiB]=0x%08x, p[5MiB]=0x%08x (intact)\n",
                p[0], p[1024 * 1024 / 4], p[5 * 1024 * 1024 / 4]);
    std::vector<unsigned char> vec(kBytes / kPage);
    resident_pages(p, kBytes, vec.data());
    print_bitmap(vec.data(), 16, "first 64 KiB:");
    print_bitmap(vec.data() + (kBytes / kPage - 16), 16, "last 64 KiB:");
    ::munmap(p, kBytes);
}

// ---- (C) MADV_DONTFORK ----
unsigned int* g_df = nullptr;  // DONTFORK 页
unsigned int* g_cow = nullptr; // 对照:普通匿名页

bool maps_has_range(const void* p, std::size_t len) {
    std::FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f) {
        return false;
    }
    char line[512];
    bool found = false;
    const auto lo = reinterpret_cast<std::uintptr_t>(p);
    const auto hi = lo + len;
    while (std::fgets(line, sizeof line, f)) {
        std::uintptr_t s = 0, e = 0;
        if (std::sscanf(line, "%lx-%lx", &s, &e) == 2 && e > lo && s < hi) {
            found = true;
            break;
        }
    }
    std::fclose(f);
    return found;
}

void on_sigsegv(int, siginfo_t* info, void*) {
    write_all("\n[handler] SIGSEGV reading DONTFORK page, si_addr = ");
    write_hex(reinterpret_cast<std::uintptr_t>(info->si_addr));
    write_all(info->si_code == SEGV_MAPERR
                  ? ", si_code = 1 (SEGV_MAPERR)  <- 子进程里地址根本不在映射里\n"
                  : ", si_code = ?\n");
    ::_exit(83);
}

void part_c_dontfork() {
    std::printf("\n== (C) MADV_DONTFORK ==\n");
    g_df = static_cast<unsigned int*>(
        ::mmap(nullptr, kPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    g_cow = static_cast<unsigned int*>(
        ::mmap(nullptr, kPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    g_df[0] = 0x5A5A5A5Au;
    g_cow[0] = 0xA5A5A5A5u;
    if (::madvise(g_df, kPage, MADV_DONTFORK) != 0) {
        std::printf("madvise DONTFORK failed errno=%d\n", errno);
        return;
    }
    std::printf("   parent: DONTFORK page %p = 0x%08x, COW page %p = 0x%08x\n",
                static_cast<void*>(g_df), g_df[0], static_cast<void*>(g_cow), g_cow[0]);

    std::fflush(stdout);
    const pid_t pid = ::fork();
    if (pid == 0) {
        struct sigaction sa{};
        sa.sa_sigaction = on_sigsegv;
        sa.sa_flags = SA_SIGINFO;
        ::sigaction(SIGSEGV, &sa, nullptr);
        const bool has_df = maps_has_range(g_df, kPage);
        const bool has_cow = maps_has_range(g_cow, kPage);
        std::printf("   child : DONTFORK page in maps? %s ; COW page in maps? %s\n",
                    has_df ? "yes" : "NO (gone)", has_cow ? "yes" : "NO");
        const unsigned int cow_before = g_cow[0];
        g_cow[0] = 0x11; // 子进程写自己的 COW 副本
        std::printf("   child : COW page reads 0x%08x, child writes 0x11 -> reads 0x%08x\n",
                    cow_before, g_cow[0]);
        std::fflush(stdout);
        volatile unsigned int v = g_df[0]; // -> SIGSEGV MAPERR
        (void)v;
        ::_exit(0);
    }
    int st = 0;
    ::waitpid(pid, &st, 0);
    std::printf("   child exited %d; parent after: DONTFORK page = 0x%08x, COW page = 0x%08x"
                " (COW 各改各的)\n",
                WIFEXITED(st) ? WEXITSTATUS(st) : -WTERMSIG(st), g_df[0], g_cow[0]);
    ::munmap(g_df, kPage);
    ::munmap(g_cow, kPage);
}

// ---- (D) MADV_REMOVE ----
void part_d_remove() {
    std::printf("\n== (D) MADV_REMOVE ==\n");
    // 私有匿名:EINVAL
    void* p = ::mmap(nullptr, kPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int rc = ::madvise(p, kPage, MADV_REMOVE);
    std::printf("   anon private : rc=%d errno=%d (%s)\n", rc, errno, std::strerror(errno));
    ::munmap(p, kPage);

    // ext4 文件映射:man 页说 MADV_REMOVE 只给 tmpfs/shmem,实测这台 6.18 内核
    // 把它实现成 fallocate(PUNCH_HOLE|KEEP_SIZE),ext4 一样打洞
    const int fd =
        ::open("/home/charliechen/lm02_scratch/remove_ext4.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
    ::ftruncate(fd, static_cast<off_t>(3 * kPage));
    void* fp = ::mmap(nullptr, 3 * kPage, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    auto* fb = static_cast<unsigned char*>(fp);
    for (std::size_t i = 0; i < 3 * kPage; ++i) {
        fb[i] = static_cast<unsigned char>('a' + i / kPage);
    }
    struct stat fst{};
    ::fstat(fd, &fst);
    rc = ::madvise(fb + kPage, kPage, MADV_REMOVE);
    const int e2rc = rc == 0 ? 0 : errno;
    char ext4_mid[kPage] = {};
    ::pread(fd, ext4_mid, kPage, static_cast<off_t>(kPage));
    bool ext4_zeroed = true;
    for (char c : ext4_mid) {
        if (c != 0) {
            ext4_zeroed = false;
        }
    }
    struct stat fst2{};
    ::fstat(fd, &fst2);
    std::printf("   ext4 shared  : 3 pages written; madvise rc=%d errno=%d; pread middle %s;"
                " st_blocks %ld -> %ld, size=%ld (hole punched, KEEP_SIZE)\n",
                rc, e2rc, ext4_zeroed ? "all-zero" : "NOT zeroed", static_cast<long>(fst.st_blocks),
                static_cast<long>(fst2.st_blocks), static_cast<long>(fst2.st_size));
    ::munmap(fp, 3 * kPage);
    ::close(fd);
    ::unlink("/home/charliechen/lm02_scratch/remove_ext4.bin");

    // /dev/shm(tmpfs):真打洞
    const char* shm_path = "/dev/shm/lm02_e2_remove.bin";
    const int sfd = ::open(shm_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (sfd < 0) {
        std::printf("   open %s failed errno=%d -- /dev/shm 不可用,如实记录\n", shm_path, errno);
        return;
    }
    constexpr std::size_t kLen = 3 * kPage;
    // 不 ftruncate 就往共享映射里写 = 越过 EOF -> SIGBUS(L02 的老朋友),先把长度立起来
    if (::ftruncate(sfd, static_cast<off_t>(kLen)) != 0) {
        std::printf("   ftruncate shm failed errno=%d\n", errno);
        ::close(sfd);
        return;
    }
    void* sp = ::mmap(nullptr, kLen, PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0);
    auto* sb = static_cast<unsigned char*>(sp);
    for (std::size_t i = 0; i < kLen; ++i) {
        sb[i] = static_cast<unsigned char>('a' + i / kPage);
    }
    struct stat st{};
    struct statvfs vs{};
    ::fstat(sfd, &st);
    ::fstatvfs(sfd, &vs);
    std::printf("   tmpfs shared: 3 pages 'a','b','c'; size=%ld, st_blocks=%ld (512B units),"
                " shm f_bfree=%lu\n",
                static_cast<long>(st.st_size), static_cast<long>(st.st_blocks),
                static_cast<unsigned long>(vs.f_bfree));
    rc = ::madvise(static_cast<unsigned char*>(sp) + kPage, kPage, MADV_REMOVE); // 打掉中间页
    const int e3 = rc == 0 ? 0 : errno;
    struct stat st2{}, st3{};
    ::fstat(sfd, &st2);                  // 先 stat 再读:读打洞页会让 shmem 给这页再立一个零页
    const char map_mid_peek = sb[kPage]; // 通过映射"读一眼"打洞页
    ::fstat(sfd, &st3);                  // 读完再看 blocks:又占回去了
    char file_mid[kPage] = {};
    ::pread(sfd, file_mid, kPage, static_cast<off_t>(kPage)); // pread 不分配
    bool file_zeroed = true;
    for (char c : file_mid) {
        if (c != 0) {
            file_zeroed = false;
        }
    }
    std::printf("   madvise(middle page, MADV_REMOVE) rc=%d errno=%d\n", rc, e3);
    std::printf("   after punch: size=%ld (unchanged); st_blocks %ld -> %ld (page freed),"
                " then read hole via mapping -> st_blocks back to %ld (shmem 立了零页)\n",
                static_cast<long>(st2.st_size), static_cast<long>(st.st_blocks),
                static_cast<long>(st2.st_blocks), static_cast<long>(st3.st_blocks));
    std::printf("   map page0='%c' page1=0x%02x page2='%c'; pread middle page %s\n", sb[0],
                static_cast<unsigned char>(map_mid_peek), sb[2 * kPage],
                file_zeroed ? "reads all-zero (hole)" : "NOT zeroed");
    ::munmap(sp, kLen);
    ::close(sfd);
    ::unlink(shm_path);
}

} // namespace

int main() {
    std::printf("E2: madvise family (page size %ld, ext4 backing file %s)\n",
                ::sysconf(_SC_PAGE_SIZE), kRaPath);
    part_a_readahead();
    part_b_dontneed_anon();
    part_c_dontfork();
    part_d_remove();
    std::printf("\ndone\n");
    return 0;
}
