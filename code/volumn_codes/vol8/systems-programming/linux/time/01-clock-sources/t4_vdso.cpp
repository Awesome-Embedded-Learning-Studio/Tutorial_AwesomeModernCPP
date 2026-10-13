// E4: vDSO —— clock_gettime 免陷入的实证:maps 里的映射 + 每次读取的代价 + strace 下的零系统调用
// 口径: 同 E1;strace 对照另见 t4_vdso_strace.txt(2M 次读取挂 strace 跑)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/syscall.h>
#include <unistd.h>

namespace {

uint64_t now_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

// 读 /proc/self/maps,挑出 [vdso] 与 [vvar] 两行
void print_mappings() {
    FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f)
        return;
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
        if (std::strstr(line, "[vdso") || std::strstr(line, "[vvar")) {
            std::fputs(line, stdout);
        }
    }
    std::fclose(f);
}

} // namespace

int main() {
    std::printf("== E4a: 本进程地址空间里的内核映射(/proc/self/maps) ==\n");
    print_mappings();
    std::printf(
        "vdso 一行就是内核提前映射进来的代码与数据;vvar 是它只读的数据页(时钟源的换算参数)\n");

    constexpr int kN = 10000000;

    std::printf("\n== E4b: 每次读取的代价(%d 次) ==\n", kN);
    timespec ts{};
    auto t0 = now_ns();
    for (int i = 0; i < kN; ++i)
        clock_gettime(CLOCK_MONOTONIC, &ts); // vDSO 路径
    auto t1 = now_ns();
    for (int i = 0; i < kN; ++i)
        syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts); // 强制走 syscall(2) 真陷入
    auto t2 = now_ns();
    double vdso = double(t1 - t0) / kN, traps = double(t2 - t1) / kN;
    std::printf("clock_gettime(vDSO)   : %.1f ns/次\n", vdso);
    std::printf("syscall(SYS_clock_gettime): %.1f ns/次(强制真陷入)\n", traps);
    std::printf("倍数: %.1fx —— 同一个函数,免陷入的差距就在这里\n", traps / vdso);

    std::printf("\n== E4c: 证据链怎么读 ==\n");
    std::printf("1) maps 里 [vdso]/[vvar] 在场;\n");
    std::printf("2) 代价差出一个数量级上下(与 00-overview 的「一次系统调用≈一两百 ns」对表);\n");
    std::printf("3) strace 挂上看不到 clock_gettime 系统调用(t4_vdso_strace.txt:vDSO 版 0 次,\n");
    std::printf("   syscall 版 2,000,000 次,同一个程序两种编法)。\n");
    return 0;
}
