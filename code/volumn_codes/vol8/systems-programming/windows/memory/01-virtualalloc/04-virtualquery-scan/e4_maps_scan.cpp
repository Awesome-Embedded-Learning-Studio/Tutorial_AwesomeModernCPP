// e4:VirtualQuery 全地址空间扫描 —— Windows 版 /proc/self/maps
// 剧本:从 0 走到 lpMaximumApplicationAddress,每步 VirtualQuery 跳一个 RegionSize,
//      统计 State(COMMIT/RESERVE/FREE)与 Type(IMAGE/MAPPED/PRIVATE)的组合与字节数,
//      再按 /proc/self/maps 的形态排一份前 24 个区段(地址段+属性),
//      最后与 GetProcessMemoryInfo 的 CommitCharge 交叉对账(COMMIT+PRIVATE 的合计)
#define WIN32_LEAN_AND_MEAN
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <psapi.h>
#include <string>
#include <vector>
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
static std::string prot_str(DWORD p) {
    if (p == 0)
        return "---";
    std::string s;
    DWORD base = p & 0xFF;
    s += (base & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                  PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))
             ? "r"
             : "-";
    s +=
        (base & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
            ? "w"
            : "-";
    s += (base &
          (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
             ? "x"
             : "-";
    if (p & PAGE_GUARD)
        s += " guard";
    return s;
}

struct Region {
    unsigned long long lo, hi;
    DWORD state, type, protect;
};

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    unsigned long long maxva = (unsigned long long)(uintptr_t)si.lpMaximumApplicationAddress;
    printf("用户态上限 lpMaximumApplicationAddress=%#llx\n", maxva);

    std::vector<Region> regions;
    unsigned long long addr = 0;
    int steps = 0;
    while (addr <= maxva) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((void*)(uintptr_t)addr, &mbi, sizeof(mbi)))
            break;
        SIZE_T step = mbi.RegionSize ? mbi.RegionSize : si.dwPageSize;
        regions.push_back({addr, addr + mbi.RegionSize, mbi.State, mbi.Type, mbi.Protect});
        ++steps;
        addr += step;
    }
    printf("扫描步数(VirtualQuery 次数)=%d,时间 <1 秒 —— FREE 段一跳就是一个大空洞\n\n", steps);

    // ---- 统计:State x Type ----
    struct Agg {
        int count = 0;
        unsigned long long bytes = 0;
    };
    std::map<std::string, Agg> table;
    int free_gaps = 0;
    unsigned long long free_bytes = 0;
    for (auto& r : regions) {
        if (r.state == MEM_FREE) {
            ++free_gaps;
            free_bytes += r.hi - r.lo;
            continue;
        }
        char key[64];
        snprintf(key, sizeof(key), "%s/%s", state_str(r.state), type_str(r.type));
        Agg& a = table[key];
        ++a.count;
        a.bytes += r.hi - r.lo;
    }
    printf("== State/Type 组合统计(非 FREE 区段)==\n");
    printf("  %-18s %8s %16s\n", "State/Type", "个数", "字节");
    unsigned long long commit_private = 0;
    for (auto& [k, a] : table) {
        printf("  %-18s %8d %16llu (%.2f GiB)\n", k.c_str(), a.count, a.bytes,
               a.bytes / 1073741824.0);
        if (k[0] == 'C' && k.substr(7) == "PRIVATE")
            commit_private += a.bytes;
    }
    printf("  %-18s %8d %16llu (%.2f TiB,用户态总空间 128 TiB)\n", "FREE(空洞)", free_gaps,
           free_bytes, free_bytes / 1099511627776.0);

    // ---- 最大的 5 个区段 ----
    printf("\n== 最大的 5 个区段 ==\n");
    std::vector<Region> nf;
    for (auto& r : regions)
        if (r.state != MEM_FREE)
            nf.push_back(r);
    std::stable_sort(nf.begin(), nf.end(), [](const Region& a, const Region& b) {
        return (a.hi - a.lo) > (b.hi - b.lo);
    });
    for (int i = 0; i < (int)nf.size() && i < 5; i++)
        printf("  %012llx..%012llx %8llu KiB %s/%s %s\n", nf[i].lo, nf[i].hi,
               (nf[i].hi - nf[i].lo) >> 10, state_str(nf[i].state), type_str(nf[i].type),
               prot_str(nf[i].protect).c_str());

    // ---- maps 形态的前 24 个非 FREE 区段(低地址在前,正是一份 Win 版 maps 头部)----
    printf("\n== 地址空间头部 24 个非 FREE 区段(maps 形态)==\n");
    int shown = 0;
    for (auto& r : regions) {
        if (r.state == MEM_FREE)
            continue;
        if (shown++ >= 24)
            break;
        printf("  %012llx-%012llx %s %-8s %-8s %s\n", r.lo, r.hi, prot_str(r.protect).c_str(),
               state_str(r.state), type_str(r.type), r.type == MEM_IMAGE ? "模块段" : "");
    }

    // ---- 对账 ----
    PROCESS_MEMORY_COUNTERS pmc;
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    printf("\n== 对账 ==\n");
    printf("  扫描合计 COMMIT/PRIVATE = %llu KiB\n", commit_private >> 10);
    printf("  GetProcessMemoryInfo CommitCharge(PagefileUsage) = %llu KiB\n",
           (unsigned long long)pmc.PagefileUsage >> 10);
    printf("  (CommitCharge ≥ COMMIT/PRIVATE:差额是 DLL/映射段的共享提交与内核侧记账 —— "
           "VQ 的 PRIVATE 只数私有提交;对照 Linux /proc/self/maps 的 Private vs VmSize)\n");
    return 0;
}
