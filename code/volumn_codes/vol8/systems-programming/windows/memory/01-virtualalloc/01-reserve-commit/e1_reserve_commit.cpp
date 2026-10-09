// e1:VirtualAlloc 保留/提交全景 —— 纯内存视角,无文件参与(W02 的映射视角对照面)
// 剧本:
//   步骤1  MEM_RESERVE 1GB:瞬间成功,提交费用分文未动(WorkingSet/CommitCharge 三段采样)
//   步骤2  子区间 MEM_COMMIT + 触碰:State 翻 COMMIT;提交≠占物理(WS 只长触碰过的页)
//   步骤3  MEM_COMMIT 直接分配(不预保留):连发三笔,空洞够就连号;各自独立预约
//          (AllocBase 各异,VQ 不把相邻同属性块合并成一段 —— 分组键里含 AllocationBase);
//          粒度块内没提交的尾巴是 FREE,下一个分配紧挨着继续发
//   步骤4  释放语义:MEM_DECOMMIT 退回保留态(可再提交) vs MEM_RELEASE
//          必须整块基址 + dwSize 必须为 0(部分释放实测报错)
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <psapi.h>
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
static const char* type_str(DWORD t) {
    switch (t) {
        case MEM_IMAGE:
            return "IMAGE";
        case MEM_MAPPED:
            return "MAPPED";
        case MEM_PRIVATE:
            return "PRIVATE";
    }
    return "-";
}
static void query_at(const char* tag, void* p) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) {
        printf("  [VQ %s] 失败 err=%lu\n", tag, GetLastError());
        return;
    }
    printf("  [VQ %-14s] %p..%p State=%-7s Protect=%#06lx Type=%-8s AllocBase=%p "
           "RegionSize=%#010llx\n",
           tag, mbi.BaseAddress, (char*)mbi.BaseAddress + mbi.RegionSize, state_str(mbi.State),
           mbi.Protect, type_str(mbi.Type), mbi.AllocationBase, (unsigned long long)mbi.RegionSize);
}
static void mem_snapshot(const char* tag) {
    PROCESS_MEMORY_COUNTERS pmc;
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    printf("  [采样 %-10s] WorkingSet=%llu KiB   CommitCharge(PagefileUsage)=%llu KiB\n", tag,
           (unsigned long long)pmc.WorkingSetSize >> 10,
           (unsigned long long)pmc.PagefileUsage >> 10);
}
static void print_err(const char* tag, BOOL ok) {
    if (ok) {
        printf("  %s -> 成功\n", tag);
        return;
    }
    DWORD e = GetLastError();
    const char* name = e == 87    ? "ERROR_INVALID_PARAMETER"
                       : e == 487 ? "ERROR_INVALID_ADDRESS"
                                  : "?";
    printf("  %s -> 失败 GetLastError=%lu(%s)\n", tag, e, name);
}

static const SIZE_T GB = 0x40000000ULL;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    printf("页=%lu 字节  分配粒度=%lu 字节  用户态上限=%p\n\n", si.dwPageSize,
           si.dwAllocationGranularity, si.lpMaximumApplicationAddress);

    printf("== 步骤1:MEM_RESERVE 1 GiB(只预约地址,不占提交额度)==\n");
    mem_snapshot("预约前");
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    char* base = (char*)VirtualAlloc(NULL, GB, MEM_RESERVE, PAGE_NOACCESS);
    QueryPerformanceCounter(&t1);
    if (!base) {
        printf("预约失败 err=%lu\n", GetLastError());
        return 1;
    }
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    printf("  VirtualAlloc(NULL,1GiB,MEM_RESERVE) = %p  耗时 %lld 微秒\n", base,
           (t1.QuadPart - t0.QuadPart) * 1000000 / f.QuadPart);
    mem_snapshot("预约后");
    query_at("base", base);
    query_at("base+1GiB-1", base + GB - si.dwPageSize);

    printf("\n== 步骤2:子区间提交 + 触碰 ==\n");
    print_err("提交 base..base+64KiB(RW)",
              VirtualAlloc(base, 0x10000, MEM_COMMIT, PAGE_READWRITE) == base);
    print_err("提交 base+1MiB..+8KiB(RW)",
              VirtualAlloc(base + 0x100000, 0x2000, MEM_COMMIT, PAGE_READWRITE) ==
                  (void*)(base + 0x100000));
    mem_snapshot("提交后未触碰");
    query_at("base", base);
    query_at("base+64KiB", base + 0x10000);
    query_at("base+1MiB", base + 0x100000);
    // 触碰:第一个区间逐页写 16 页;第二个区间只写第 0、1 页
    for (int i = 0; i < 16; i++)
        base[i * si.dwPageSize] = (char)i;
    base[0x100000] = 1;
    base[0x100000 + si.dwPageSize] = 1;
    mem_snapshot("触碰后");
    printf("  (提交 +72KiB 在先,WS 只长了被摸过的 ~18 页 —— 提交是承诺,触碰才占物理)\n");

    printf("\n== 步骤3:MEM_COMMIT 直接分配(不预保留)==\n");
    char* p[3];
    for (int i = 0; i < 3; i++) {
        p[i] = (char*)VirtualAlloc(NULL, 0x10000, MEM_COMMIT, PAGE_READWRITE);
        printf("  p%d = %p\n", i + 1, p[i]);
    }
    printf("  p2-p1=%#llx  p3-p2=%#llx%s\n", (unsigned long long)(p[1] - p[0]),
           (unsigned long long)(p[2] - p[1]),
           (p[1] - p[0] == 0x10000 && p[2] - p[1] == 0x10000)
               ? "(相邻 64KiB 粒度块连号)"
               : "(本轮没连上 —— 中间被别的分配占了空洞)");
    query_at("p1", p[0]);
    query_at("p2", p[1]); // 实测:相邻同属性但 AllocBase 各异 → VQ 不合并,各报各的 0x10000
    char* q1 = (char*)VirtualAlloc(NULL, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
    char* q2 = (char*)VirtualAlloc(NULL, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
    printf("  q1=%p q2=%p(各要 1 页,地址仍隔 64KiB)\n", q1, q2);
    query_at("q1", q1);
    query_at("q1+4KiB", q1 + si.dwPageSize); // 粒度块内未提交的尾部:实测 FREE(不是隐藏预约)
    query_at("q1+128KiB(下个粒度)", q1 + 0x20000);

    printf("\n== 步骤4:释放语义 ==\n");
    print_err("VirtualFree(base+64KiB,64KiB,MEM_RELEASE)",
              VirtualFree(base + 0x10000, 0x10000, MEM_RELEASE));
    print_err("VirtualFree(base,4KiB,MEM_RELEASE)(dwSize 非 0)",
              VirtualFree(base, 0x1000, MEM_RELEASE));
    print_err("VirtualFree(base,64KiB,MEM_DECOMMIT)", VirtualFree(base, 0x10000, MEM_DECOMMIT));
    query_at("base(退提交后)", base);
    mem_snapshot("退提交后");
    print_err("再提交 base..+64KiB(退提交可逆)",
              VirtualAlloc(base, 0x10000, MEM_COMMIT, PAGE_READWRITE) == (void*)base);
    query_at("base(再提交后)", base);
    print_err("VirtualFree(base,0,MEM_RELEASE)(整块归还,含仍在提交的 +1MiB 子区间)",
              VirtualFree(base, 0, MEM_RELEASE));
    query_at("base(归还后)", base);
    // 步骤3 的独立预约逐个整块归还:p1 释放后 p2/p3 的合并视图应缩成 0x20000
    print_err("VirtualFree(p1,0,MEM_RELEASE)", VirtualFree(p[0], 0, MEM_RELEASE));
    query_at("p1(已 FREE)", p[0]);
    query_at("p2(仍在)", p[1]);
    VirtualFree(p[1], 0, MEM_RELEASE);
    VirtualFree(p[2], 0, MEM_RELEASE);
    VirtualFree(q1, 0, MEM_RELEASE);
    VirtualFree(q2, 0, MEM_RELEASE);
    printf("  (p1 只放掉自己的 0x10000 —— 每笔 VirtualAlloc 是独立预约,相邻≠一体)\n");
    return 0;
}
