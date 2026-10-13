// E3b: 探针 —— 为什么 MADV_HUGEPAGE 的 VMA(flags 含 hg)却 AnonHugePages=0?
// 诊断链:sysfs 都正常(enabled=[madvise],2048kB 继承)→ 但 /proc/self 的 VMA
// THPeligible=0、vmstat 计数器全 0、MADV_COLLAPSE 报 EINVAL。
// 根因:prctl(PR_GET_THP_DISABLE)=1 —— WSL2 的 init 链给整个进程树设了
// MMF_DISABLE_THP(配套动态内存/气球机制)。本内核(6.18)允许
// prctl(PR_SET_THP_DISABLE, 0) 清掉它,清完 THP 立刻复活,正反对照一次拿全。
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>

static constexpr size_t kMiB = 1ull << 20;

static std::string slurp(const char* path) {
    std::ifstream f(path);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    while (!s.empty() && (s.back() == '\n' || s.back() == ' '))
        s.pop_back();
    return s;
}

static bool parse_header(const std::string& line, unsigned long* lo, unsigned long* hi) {
    return std::sscanf(line.c_str(), "%lx-%lx", lo, hi) == 2 && line.find('-') < line.find(' ');
}

static void smaps_fields(void* addr, const char* tag) {
    std::uintptr_t a = reinterpret_cast<std::uintptr_t>(addr);
    std::ifstream f("/proc/self/smaps");
    std::string line;
    bool mine = false;
    while (std::getline(f, line)) {
        unsigned long lo = 0, hi = 0;
        if (parse_header(line, &lo, &hi)) {
            mine = (a >= lo && a < hi);
            continue;
        }
        if (mine && (line.rfind("AnonHugePages:", 0) == 0 || line.rfind("THPeligible:", 0) == 0))
            std::printf("    %s %s\n", tag, line.c_str());
    }
}

// /proc/vmstat 计数器是开机累计的,这里以进程启动时为基线打差值
static long long g_base[4] = {0, 0, 0, 0};
static const char* g_keys[4] = {"thp_fault_alloc", "thp_fault_fallback", "thp_collapse_alloc",
                                "nr_anon_transparent_hugepages"};

static void snapshot_base() {
    std::ifstream f("/proc/vmstat");
    std::string k;
    long long v;
    while (f >> k >> v)
        for (int i = 0; i < 4; ++i)
            if (k == g_keys[i])
                g_base[i] = v;
}

static void thp_counters(const char* when) {
    std::printf("---- %s(相对本进程启动的增量) ----\n", when);
    std::ifstream f("/proc/vmstat");
    std::string k;
    long long v;
    while (f >> k >> v)
        for (int i = 0; i < 4; ++i)
            if (k == g_keys[i])
                std::printf("    %-30s %+lld\n", k.c_str(), v - g_base[i]);
}

int main() {
    std::printf("sysfs: enabled=%s  2048kB/enabled=%s  shmem_enabled=%s\n",
                slurp("/sys/kernel/mm/transparent_hugepage/enabled").c_str(),
                slurp("/sys/kernel/mm/transparent_hugepage/hugepages-2048kB/enabled").c_str(),
                slurp("/sys/kernel/mm/transparent_hugepage/shmem_enabled").c_str());
    std::printf("prctl(PR_GET_THP_DISABLE) = %d   <-- WSL2 的 init 给整个进程树设了它\n\n",
                prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0));

    size_t len = 32 * kMiB;
    snapshot_base();
    thp_counters("基线");

    // ① 出厂状态:MADV_HUGEPAGE 也拿不到大页
    unsigned char* p1 = static_cast<unsigned char*>(
        mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    madvise(p1, len, MADV_HUGEPAGE);
    for (size_t i = 0; i < len; i += 4096)
        p1[i] = 0x11;
    std::printf("\n① 出厂状态:mmap+MADV_HUGEPAGE+首触(%p)\n", (void*)p1);
    smaps_fields(p1, "[被关]");
    int rc = madvise(p1, len, MADV_COLLAPSE);
    std::printf("    madvise(MADV_COLLAPSE) rc=%d errno=%d(%s)  <-- 直接 EINVAL\n\n", rc, errno,
                std::strerror(errno));
    munmap(p1, len);

    // ② 清掉进程级开关,同样操作再来一遍
    prctl(PR_SET_THP_DISABLE, 0, 0, 0, 0);
    std::printf("② prctl(PR_SET_THP_DISABLE, 0) 后:PR_GET=%d\n",
                prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0));
    unsigned char* p2 = static_cast<unsigned char*>(
        mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    madvise(p2, len, MADV_HUGEPAGE);
    for (size_t i = 0; i < len; i += 4096)
        p2[i] = 0x22;
    std::printf("    mmap+MADV_HUGEPAGE+首触(%p):\n", (void*)p2);
    smaps_fields(p2, "[复活]");
    thp_counters("② 之后");

    // ③ MADV_COLLAPSE:不带 HUGEPAGE 的 4K 页先 fault,再同步捏成大页
    unsigned char* p3 = static_cast<unsigned char*>(
        mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    for (size_t i = 0; i < len; i += 4096)
        p3[i] = 0x33; // 没有 MADV_HUGEPAGE,纯 4K fault
    std::printf("\n③ 无 madvise 的 4K 首触后,直接 MADV_COLLAPSE(%p):\n", (void*)p3);
    smaps_fields(p3, "[塌缩前]");
    errno = 0;
    rc = madvise(p3, len, MADV_COLLAPSE);
    std::printf("    madvise(MADV_COLLAPSE) rc=%d errno=%d(%s)\n", rc, errno, std::strerror(errno));
    smaps_fields(p3, "[塌缩后]");
    thp_counters("③ 之后");

    unsigned long long sum = 0;
    for (size_t i = 0; i < len; i += 4096)
        sum += p2[i] + p3[i];
    std::printf("\n校验:p2 每页 0x22、p3 每页 0x33,首字节和=%llu(应=%zu*(0x22+0x33)=%llu)\n", sum,
                len / 4096, static_cast<unsigned long long>(len / 4096) * (0x22 + 0x33));
    return 0;
}
