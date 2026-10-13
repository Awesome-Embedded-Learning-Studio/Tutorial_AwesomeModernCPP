// e2:分配粒度 —— 64 KiB 分配粒度 vs 4 KiB 页粒度,两组数字必须分开念
// 剧本:
//   步骤1  要 1 字节 / 4KiB / 64KiB-1 / 64KiB / 64KiB+1 / 1MiB+1 各分配一次:
//          返回地址一律 64KiB 对齐(分配粒度),RegionSize 的取整口径另算(页粒度)
//   步骤2  连续 4 次 64KiB 分配:地址间隔恰好 0x10000(默认自底向上)
//   步骤3  MEM_TOP_DOWN:从高地址往下发,与默认模式的地址量级对比 + 连续两次递减
#define WIN32_LEAN_AND_MEAN
#include <cstdint>
#include <cstdio>
#include <windows.h>

static const char* state_str(DWORD s) {
    switch (s) {
        case MEM_COMMIT:
            return "COMMIT";
        case MEM_RESERVE:
            return "RESERVE";
        case MEM_FREE:
            return "FREE";
    }
    return "?";
}
static void probe(const char* tag, void* p) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) {
        printf("  [VQ %s] 失败\n", tag);
        return;
    }
    printf("  [VQ %-16s] State=%-7s Protect=%#06lx AllocBase=%p RegionSize=%#010llx\n", tag,
           state_str(mbi.State), mbi.Protect, mbi.AllocationBase,
           (unsigned long long)mbi.RegionSize);
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    printf("dwPageSize=%lu(页粒度)  dwAllocationGranularity=%lu(分配粒度)\n\n", si.dwPageSize,
           si.dwAllocationGranularity);

    printf("== 步骤1:请求尺寸 vs 实得地址/RegionSize(MEM_RESERVE|MEM_COMMIT)==\n");
    printf("  %-12s %-18s %-8s %-10s %s\n", "请求", "返回地址", "低16位", "64K对齐",
           "RegionSize(VQ)");
    const SIZE_T sizes[] = {1, 0x1000, 0xFFFF, 0x10000, 0x10001, 0x100001};
    void* keep[8];
    int nkeep = 0;
    for (SIZE_T s : sizes) {
        char* p = (char*)VirtualAlloc(NULL, s, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!p) {
            printf("  %#-12llx 失败 err=%lu\n", (unsigned long long)s, GetLastError());
            continue;
        }
        MEMORY_BASIC_INFORMATION mbi;
        VirtualQuery(p, &mbi, sizeof(mbi));
        unsigned long long low16 = (unsigned long long)(uintptr_t)p & 0xFFFF;
        printf("  %#-12llx %-18p %#-8llx %-10s %#010llx(%llu 页)\n", (unsigned long long)s, p,
               low16, low16 == 0 ? "是" : "否", (unsigned long long)mbi.RegionSize,
               (unsigned long long)(mbi.RegionSize / si.dwPageSize));
        keep[nkeep++] = p;
    }
    // 小尺寸的粒度块尾部探针:1 字节实得 0x10000 的块,块内没提交的部分是什么状态
    printf("\n  探针:上面 1 字节那笔(%p)块内偏移 +4KiB 处:\n", keep[0]);
    probe("+4KiB", (char*)keep[0] + si.dwPageSize);
    probe("+64KiB(块外)", (char*)keep[0] + 0x10000);

    printf("\n== 步骤2:默认模式连发 4 次 64KiB(自底向上连号)==\n");
    char* a[4];
    for (int i = 0; i < 4; i++)
        a[i] = (char*)VirtualAlloc(NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    for (int i = 0; i < 4; i++)
        printf("  a%d=%p%s", i, a[i], i < 3 ? "\n" : "");
    printf("  <- 间隔:%#llx %#llx %#llx\n", (unsigned long long)(a[1] - a[0]),
           (unsigned long long)(a[2] - a[1]), (unsigned long long)(a[3] - a[2]));
    if (a[1] - a[0] != 0x10000) // 间隔被撑开:看看谁在 a0 与 a1 之间插了队
        probe("a0+64KiB(间隔里)", a[0] + 0x10000);

    printf("\n== 步骤3:MEM_TOP_DOWN(从高往低发)==\n");
    char* low = (char*)VirtualAlloc(NULL, 0x100000, MEM_RESERVE, PAGE_NOACCESS);
    char* td1 = (char*)VirtualAlloc(NULL, 0x100000, MEM_RESERVE | MEM_TOP_DOWN, PAGE_NOACCESS);
    char* td2 = (char*)VirtualAlloc(NULL, 0x100000, MEM_RESERVE | MEM_TOP_DOWN, PAGE_NOACCESS);
    printf("  默认     low =%p\n", low);
    printf("  TOP_DOWN td1=%p\n", td1);
    printf("  TOP_DOWN td2=%p\n", td2);
    printf("  td1-low=%#llx(跨了半个用户态空间,量级对比)\n", (unsigned long long)(td1 - low));
    printf("  td1-td2=%#llx(第二次 TOP_DOWN 更低 —— 高水位往下走,%s)\n",
           (unsigned long long)(td1 - td2),
           td2 < td1 ? ((td1 - td2) == 0x100000 ? "间隔恰 1MiB" : "间隔见前值") : "本机未递减");
    printf("  td1/td2 低16位:%#llx / %#llx\n", (unsigned long long)(uintptr_t)td1 & 0xFFFF,
           (unsigned long long)(uintptr_t)td2 & 0xFFFF);

    for (int i = 0; i < nkeep; i++)
        VirtualFree(keep[i], 0, MEM_RELEASE);
    VirtualFree(low, 0, MEM_RELEASE);
    VirtualFree(td1, 0, MEM_RELEASE);
    VirtualFree(td2, 0, MEM_RELEASE);
    for (int i = 0; i < 4; i++)
        VirtualFree(a[i], 0, MEM_RELEASE);
    return 0;
}
