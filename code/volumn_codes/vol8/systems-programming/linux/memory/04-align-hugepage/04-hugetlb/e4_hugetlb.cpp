// E4: 显式大页(hugetlb)—— 无特权环境的能力边界
// 路径:/proc/sys/vm/nr_hugepages(内核启动时预留的 2 MiB 大页池)。
// 本机 nr_hugepages=0(HugePages_Total=0),往里写要 root;无预留时
// mmap MAP_HUGETLB 预期 ENOMEM。全程如实记录,不做任何提权尝试。
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

static std::string slurp(const char* path) {
    std::ifstream f(path);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    while (!s.empty() && (s.back() == '\n' || s.back() == ' '))
        s.pop_back();
    return s;
}

static void try_mmap(const char* tag, size_t len, int extra_flags) {
    errno = 0;
    void* p = mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | extra_flags,
                   -1, 0);
    if (p == MAP_FAILED) {
        std::printf("%-38s -> MAP_FAILED  errno=%d (%s)\n", tag, errno, std::strerror(errno));
    } else {
        std::printf("%-38s -> %p  成功\n", tag, p);
        std::memset(p, 1, len < 4096 ? len : 4096); // 真摸一页,证明映射可用
        munmap(p, len);
    }
}

int main() {
    std::printf("nr_hugepages          = %s\n", slurp("/proc/sys/vm/nr_hugepages").c_str());
    std::printf("nr_overcommit_hugepages = %s\n",
                slurp("/proc/sys/vm/nr_overcommit_hugepages").c_str());

    std::printf("/proc/meminfo 大页相关字段:\n");
    {
        std::ifstream f("/proc/meminfo");
        std::string line;
        while (std::getline(f, line))
            if (line.rfind("Huge", 0) == 0 || line.rfind("Hugetlb", 0) == 0)
                std::printf("    %s\n", line.c_str());
    }

    // 非特权用户尝试调大预留池:预期 open 失败(procfs 文件属 root,0644)
    int fd = open("/proc/sys/vm/nr_hugepages", O_WRONLY);
    if (fd < 0)
        std::printf("open(nr_hugepages, O_WRONLY) -> 失败 errno=%d (%s)  [无 root,如实记录]\n",
                    errno, std::strerror(errno));
    else {
        std::printf("nr_hugepages 竟然可写?!\n");
        close(fd);
    }

    std::printf("\nMAP_HUGETLB 尝试(池为空,预期 ENOMEM):\n");
    try_mmap("MAP_HUGETLB|MAP_HUGE_2MB, 2 MiB", 2u << 20, MAP_HUGETLB | (21 << MAP_HUGE_SHIFT));
    try_mmap("MAP_HUGETLB|MAP_HUGE_1GB, 1 GiB", 1ull << 30, MAP_HUGETLB | (30 << MAP_HUGE_SHIFT));
    try_mmap("MAP_HUGETLB(默认页大小), 2 MiB", 2u << 20, MAP_HUGETLB);

    std::printf("\n对照组(不带 HUGETLB,普通匿名映射):\n");
    try_mmap("普通 MAP_ANONYMOUS, 2 MiB", 2u << 20, 0);

    std::printf("\n结论口径:显式大页需要管理员先往 nr_hugepages 池里预留(启动参数或 root 写\n"
                "sysctl,或挂 hugetlbfs);无特权环境下能走的大页路径是 E3 的 THP(madvise)。\n");
    return 0;
}
