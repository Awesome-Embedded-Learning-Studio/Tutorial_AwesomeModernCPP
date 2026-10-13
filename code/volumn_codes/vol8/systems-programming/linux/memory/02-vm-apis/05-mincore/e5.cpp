// E5: mincore —— 页驻留状态的位图问询
//   匿名 8 页的一生:没摸过全 0 -> 写 0/3/7 三页 -> 只读未写的页(共享零页
//   算不算驻留?) -> MADV_DONTNEED 后归 0(与 E2 呼应) -> 再读再写的位图变化;
//   文件页的一生:fadvise 压干净 -> WILLNEED 预读 -> DONTNEED 单页丢弃。
//   另观察 mincore 与 smaps Rss 对"零页"口径的分叉(如实记录)。
#include "article.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr std::size_t kPage = 4096;

long smaps_rss_kb(const void* p) {
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
                std::strcmp(name, "Rss") == 0) {
                val = v;
                break;
            }
        }
    }
    std::fclose(f);
    return val;
}

void bitmap(void* p, std::size_t npages, const char* label) {
    std::vector<unsigned char> vec(npages);
    if (::mincore(p, npages * kPage, vec.data()) != 0) {
        std::printf("   mincore failed errno=%d\n", errno);
        return;
    }
    std::size_t n = 0;
    for (std::size_t i = 0; i < npages; ++i) {
        n += vec[i] & 1;
    }
    std::printf("   %-34s ", label);
    for (std::size_t i = 0; i < npages; ++i) {
        std::printf("%c", (vec[i] & 1) ? '1' : '0');
    }
    std::printf("  (%zu/%zu resident, Rss=%ld kB)\n", n, npages, smaps_rss_kb(p));
}

} // namespace

int main() {
    std::printf("E5: mincore residency bitmap (page %ld)\n", ::sysconf(_SC_PAGE_SIZE));

    // ---- 匿名页的一生 ----
    std::printf("\n== anonymous 8 pages, page by page ==\n");
    auto* p = static_cast<unsigned int*>(
        ::mmap(nullptr, 8 * kPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    bitmap(p, 8, "1) fresh mmap, untouched:");

    p[0] = 0x11111111;
    p[3 * kPage / 4] = 0x44444444;
    p[7 * kPage / 4] = 0x77777777;
    bitmap(p, 8, "2) wrote pages 0,3,7:      ");

    const unsigned int only_read = p[kPage / 4]; // 只读第 1 页:从未写过
    std::printf("   read page 1 (never written) -> 0x%08x\n", only_read);
    bitmap(p, 8, "3) after READ of page 1:   ");
    std::printf("   ^ 零页也算驻留(present),但 Rss 没涨:mincore 数的是 PTE present,\n"
                "     smaps Rss 只数真的分给这个 VMA 的物理页,共享零页不算 -- 口径分叉,如实记录\n");

    if (::madvise(p + 3 * kPage / 4, kPage / 4, MADV_DONTNEED) != 0) {
        std::printf("madvise failed errno=%d\n", errno);
    }
    std::printf("   madvise(page 3, MADV_DONTNEED) rc=0\n");
    bitmap(p, 8, "4) after DONTNEED page 3:  ");
    std::printf("   page 3 reads back 0x%08x (zeroed)\n", p[3 * kPage / 4]);
    bitmap(p, 8, "5) after READ page 3:      ");

    p[3 * kPage / 4] = 0x55555555; // 重新写
    bitmap(p, 8, "6) rewrote page 3:         ");

    ::munmap(p, 8 * kPage);

    // ---- 文件页的一生(与 E2 的 WILLNEED/DONTNEED 呼应) ----
    std::printf("\n== file-backed 2 pages (ext4) ==\n");
    const int fd =
        ::open("/home/charliechen/lm02_scratch/mincore.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
    ::ftruncate(fd, static_cast<off_t>(2 * kPage));
    void* fp = ::mmap(nullptr, 2 * kPage, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    auto* fb = static_cast<unsigned int*>(fp);
    fb[0] = 0xAAAAAAAA;
    fb[kPage / 4] = 0xBBBBBBBB;
    ::fdatasync(fd);
    ::munmap(fp, 2 * kPage);
    ::close(fd);

    const int fd2 = ::open("/home/charliechen/lm02_scratch/mincore.bin", O_RDONLY);
    ::posix_fadvise(fd2, 0, 0, POSIX_FADV_DONTNEED); // 压干净页缓存
    fp = ::mmap(nullptr, 2 * kPage, PROT_READ, MAP_PRIVATE, fd2, 0);
    bitmap(fp, 2, "1) after fadvise DONTNEED:");
    ::madvise(fp, 2 * kPage, MADV_WILLNEED);
    ::usleep(100 * 1000); // 异步预读,稍等
    bitmap(fp, 2, "2) after MADV_WILLNEED:   ");
    std::printf("   ^ mincore 全 1 而 Rss=0:文件映射的 mincore 问的是页缓存,页缓存有了就算 1,\n"
                "     哪怕还没缺页进本进程页表 -- 和匿名页的口径不一样\n");
    const unsigned int v0 = *static_cast<unsigned int*>(fp);
    std::printf("   page 0 reads 0x%08x (now faulted into OUR page table, Rss grows)\n", v0);
    bitmap(fp, 2, "3) after reading page 0:  ");
    ::madvise(static_cast<char*>(fp) + kPage, kPage, MADV_DONTNEED); // 只丢页表那一份
    bitmap(fp, 2, "4) DONTNEED page 1:       ");
    std::printf("   ^ MADV_DONTNEED 丢的是本进程页表映射,页缓存里的文件页还在,mincore 依旧 1;\n"
                "     真把文件页从页缓存请出去要靠 fadvise(文件级),再拍一次:\n");
    ::posix_fadvise(fd2, 0, 0, POSIX_FADV_DONTNEED);
    bitmap(fp, 2, "5) fadvise DONTNEED again:");
    std::printf("   ^ page 0 还映射在本进程页表里,页缓存请不动它;page 1 无映射,请走了\n"
                "     page 1 再读还是 0x%08x(重新缺页,从磁盘/缓存回来)\n",
                *reinterpret_cast<unsigned int*>(static_cast<char*>(fp) + kPage));
    ::munmap(fp, 2 * kPage);
    ::close(fd2);
    ::unlink("/home/charliechen/lm02_scratch/mincore.bin");

    std::printf("\ndone\n");
    return 0;
}
