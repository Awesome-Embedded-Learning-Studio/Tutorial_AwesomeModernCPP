// E3: mlock / mlockall —— 锁页的四个可观察面
//   (1) mlock 一段匿名内存:/proc/self/status 的 VmLck 与 smaps 的 Locked 同步涨;
//       更要紧的是 mlock 会顺手把页摸进内存(mincore 位图在"从未写入"时已变 1);
//   (2) munlock 只解锁不逐出:位图仍全 1,VmLck 归零;
//   (3) RLIMIT_MEMLOCK:本机软硬限额(实测 64 MiB)是多少、踩线(恰好等于限额)成不成、
//       超限(限额+1 页)吃什么错误;累计锁定同样受这条线管;
//   (4) mlockall(MCL_CURRENT) 一次锁全部(RSS 在限额内就成功);
//       另附本机 swap 现状(/proc/swaps、swappiness)与实时场景的意义。
#include "article.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr std::size_t kPage = 4096;

// /proc/self/status 里的 kB 字段
long status_kb(const char* key) {
    std::FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) {
        return -1;
    }
    char line[256];
    long val = -1;
    while (std::fgets(line, sizeof line, f)) {
        char name[64] = {};
        long v = 0;
        if (std::sscanf(line, "%63[a-zA-Z_]: %ld kB", name, &v) == 2 &&
            std::strcmp(name, key) == 0) {
            val = v;
            break;
        }
    }
    std::fclose(f);
    return val;
}

// /proc/self/smaps:包含 p 的 VMA 的字段(kB)
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
        if (std::sscanf(line, "%lx-%lx", &s, &e) == 2 && e > s) {
            in_vma = target >= s && target < e;
            continue;
        }
        if (in_vma) {
            char name[64] = {};
            long v = 0;
            if (std::sscanf(line, "%63[a-zA-Z_]: %ld", name, &v) == 2 &&
                std::strcmp(name, key) == 0) {
                val = v;
                break;
            }
        }
    }
    std::fclose(f);
    return val;
}

void mincore_print(void* p, std::size_t npages, const char* label) {
    std::vector<unsigned char> vec(npages);
    if (::mincore(p, npages * kPage, vec.data()) != 0) {
        std::printf("   mincore failed errno=%d\n", errno);
        return;
    }
    std::printf("   %s bitmap: ", label);
    for (std::size_t i = 0; i < npages; ++i) {
        std::printf("%c", (vec[i] & 1) ? '1' : '0');
    }
    std::printf("\n");
}

void dump_swap_state() {
    std::printf("   /proc/swaps:\n");
    std::FILE* f = std::fopen("/proc/swaps", "r");
    if (f) {
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            std::printf("     %s", line);
        }
        std::fclose(f);
    }
    std::printf("   /proc/sys/vm/swappiness: ");
    f = std::fopen("/proc/sys/vm/swappiness", "r");
    if (f) {
        char line[64];
        while (std::fgets(line, sizeof line, f)) {
            std::printf("%s", line);
        }
        std::fclose(f);
    }
}

} // namespace

int main() {
    std::printf("E3: mlock / mlockall (page size %ld)\n", ::sysconf(_SC_PAGE_SIZE));

    struct rlimit rl{};
    ::getrlimit(RLIMIT_MEMLOCK, &rl);
    std::printf("RLIMIT_MEMLOCK: soft=%lu hard=%lu bytes\n",
                static_cast<unsigned long>(rl.rlim_cur), static_cast<unsigned long>(rl.rlim_max));

    // ---- (1) mlock:VmLck/Locked 涨,还顺手预故障 ----
    std::printf("\n== (1) mlock 2 pages of an 8-page anon mapping (never touched) ==\n");
    unsigned char* p = static_cast<unsigned char*>(
        ::mmap(nullptr, 8 * kPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    mincore_print(p, 8, "before mlock ");
    std::printf("   VmLck=%ld kB, smaps Locked=%ld kB\n", status_kb("VmLck"),
                smaps_field_kb(p, "Locked"));
    if (::mlock(p, 2 * kPage) != 0) {
        std::printf("mlock failed errno=%d (%s)\n", errno, std::strerror(errno));
        return 1;
    }
    std::printf("   mlock(p, 2 pages) rc=0  -- pages still never written by us:\n");
    mincore_print(p, 8, "after mlock  ");
    std::printf("   VmLck=%ld kB, smaps Locked=%ld kB  <- mlock 自己把页摸进了内存\n",
                status_kb("VmLck"), smaps_field_kb(p, "Locked"));
    p[0] = 0x7F;
    std::printf("   p[0]=0x%02x written; VmLck=%ld kB (unchanged)\n", p[0], status_kb("VmLck"));

    // ---- (2) munlock:解锁不逐出 ----
    std::printf("\n== (2) munlock: unlock, don't evict ==\n");
    ::munlock(p, 2 * kPage);
    mincore_print(p, 8, "after munlock");
    std::printf("   VmLck=%ld kB (back to 0), pages still resident\n", status_kb("VmLck"));

    // ---- (3) RLIMIT_MEMLOCK 边界 ----
    std::printf("\n== (3) RLIMIT_MEMLOCK boundary ==\n");
    const std::size_t limit = rl.rlim_cur; // 本机 64 MiB(zsh ulimit -l 的 65536 是 KiB)
    unsigned char* r = static_cast<unsigned char*>(
        ::mmap(nullptr, limit + kPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    int rc = ::mlock(r, limit + kPage); // 限额 + 1 页
    std::printf("   mlock(limit + 1 page = %zu B): rc=%d errno=%d (%s)\n", limit + kPage, rc,
                rc ? errno : 0, std::strerror(rc ? errno : 0));
    rc = ::mlock(r, limit); // 恰好等于限额
    std::printf("   mlock(limit exactly = %zu B): rc=%d errno=%d (%s)\n", limit, rc, rc ? errno : 0,
                std::strerror(rc ? errno : 0));
    std::printf("   VmLck=%ld kB\n", status_kb("VmLck"));
    unsigned char* one = static_cast<unsigned char*>(
        ::mmap(nullptr, kPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    rc = ::mlock(one, kPage); // 已锁满限额,再补 1 页
    std::printf("   one more page on top of a full limit: rc=%d errno=%d (%s)\n", rc,
                rc ? errno : 0, std::strerror(rc ? errno : 0));
    ::munlock(r, limit);
    ::munmap(r, limit + kPage);
    ::munmap(one, kPage);
    std::printf("   VmLck now=%ld kB\n", status_kb("VmLck"));

    // ---- (4) mlockall ----
    std::printf("\n== (4) mlockall(MCL_CURRENT) ==\n");
    std::printf("   VmRSS=%ld kB vs limit %lu bytes\n", status_kb("VmRSS"),
                static_cast<unsigned long>(rl.rlim_cur));
    rc = ::mlockall(MCL_CURRENT);
    std::printf("   mlockall(MCL_CURRENT) rc=%d errno=%d (%s)\n", rc, rc ? errno : 0,
                std::strerror(rc ? errno : 0));
    if (rc == 0) {
        std::printf("   VmLck=%ld kB\n", status_kb("VmLck"));
        ::munlockall();
    }

    // ---- swap 现状 ----
    std::printf("\n== swap on this box (why realtime wants mlock) ==\n");
    dump_swap_state();
    std::printf("   locked pages never swap out -- that is the whole pitch;"
                " forcing deterministic swap-out here would need root"
                " (drop_caches) or dangerous memory pressure, recorded as-is\n");

    ::munmap(p, 8 * kPage);
    std::printf("\ndone\n");
    return 0;
}
