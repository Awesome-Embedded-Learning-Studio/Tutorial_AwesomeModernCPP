// E5: Overcommit —— 虚拟承诺 != 物理占用
// 本机 /proc/sys/vm/overcommit_memory = 0(启发式):单次分配超过"RAM+swap 量级"会被拒,
// 512 GiB 的 malloc 预期拿 NULL;MAP_NORESERVE 明示"不计入承诺"后同一规模直接成功。
// 只写其中 1 GiB,VmRSS 只涨 ~1 GiB —— 承诺(虚拟)/占用(物理)的分野用数字钉死。
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

static void status(const char* stage) {
    std::ifstream f("/proc/self/status");
    std::string line;
    std::printf("  [%s]\n", stage);
    while (std::getline(f, line))
        if (line.rfind("VmSize:", 0) == 0 || line.rfind("VmRSS:", 0) == 0 ||
            line.rfind("VmData:", 0) == 0)
            std::printf("    %s\n", line.c_str());
}

static long meminfo_kb(const char* key) {
    std::ifstream f("/proc/meminfo");
    std::string line;
    while (std::getline(f, line))
        if (line.rfind(key, 0) == 0) {
            long v = 0;
            std::sscanf(line.c_str() + std::strlen(key), " %ld", &v);
            return v;
        }
    return -1;
}

static std::string slurp(const char* path) {
    std::ifstream f(path);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    while (!s.empty() && (s.back() == '\n' || s.back() == ' '))
        s.pop_back();
    return s;
}

int main() {
    std::printf("overcommit_memory = %s   (0=启发式 1=永远允许 2=严格按 CommitLimit)\n",
                slurp("/proc/sys/vm/overcommit_memory").c_str());
    std::printf("overcommit_ratio  = %s   (仅模式 2 用到)\n",
                slurp("/proc/sys/vm/overcommit_ratio").c_str());
    std::printf("CommitLimit=%ld kB  Committed_AS=%ld kB  MemTotal=%ld kB  SwapTotal=%ld kB\n\n",
                meminfo_kb("CommitLimit:"), meminfo_kb("Committed_AS:"), meminfo_kb("MemTotal:"),
                meminfo_kb("SwapTotal:"));

    status("阶段0 基线");

    // ① malloc 512 GiB:启发式模式下单次要超 RAM+swap(~70 GiB),预期被拒
    constexpr size_t kBig = 512ull << 30;
    errno = 0;
    void* p = std::malloc(kBig);
    std::printf("\n  malloc(512 GiB) -> %p errno=%d (%s)\n", p, errno, std::strerror(errno));
    if (p) {
        std::free(p);
        p = nullptr;
    }

    // ② 逐级下探,找启发式愿意放行的量级
    //    (打印指针本身,防 gcc -O2 把只做空判的 malloc/free 对优化掉——
    //     折叠掉的 malloc 根本不发 syscall,会记出假成功)
    for (size_t want : {256ull << 30, 128ull << 30, 64ull << 30, 32ull << 30}) {
        errno = 0;
        void* q = std::malloc(want);
        std::printf("  malloc(%3zu GiB) -> %p errno=%d(%s)\n", want >> 30, q, errno,
                    q ? "成功" : "被拒");
        std::free(q);
        if (q)
            break;
    }

    // ③ MAP_NORESERVE:同一 512 GiB,明示不做承诺记账,预期成功
    errno = 0;
    unsigned char* m = static_cast<unsigned char*>(mmap(
        nullptr, kBig, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    std::printf("\n  mmap(512 GiB, MAP_NORESERVE) -> %s errno=%d\n",
                m == MAP_FAILED ? "MAP_FAILED" : "成功", errno);
    if (m == MAP_FAILED)
        return 1;
    status("阶段3 阶段映射后,一字节没碰:VmSize +512 GiB,RSS 不动");

    // ④ 只写 1 GiB:RSS 只涨 1 GiB 量级
    constexpr size_t kTouch = 1ull << 30;
    for (size_t i = 0; i < kTouch; i += 4096)
        m[i] = 0xAA;
    status("阶段4 只写 1 GiB:VmRSS 涨到 ~1 GiB 量级,承诺的其余 511 GiB 不占物理页");

    // ⑤ 释放:munmap 立刻还给内核
    munmap(m, kBig);
    status("阶段5 munmap 后:回到基线");

    std::printf("\n口径:模式 0(本机)启发式,单次大于 RAM+swap 的申请会被拒;\n"
                "模式 1 永远放行;模式 2 严格模式,全系统 Committed_AS 不得超过 CommitLimit\n"
                "(= ratio%%×RAM + swap,本机即 50%%×~53 GiB + 16 GiB ≈ 42.5 GiB)。\n"
                "切换要写 /proc/sys/vm/overcommit_memory,无 root 做不了,只读记录。\n");
    return 0;
}
