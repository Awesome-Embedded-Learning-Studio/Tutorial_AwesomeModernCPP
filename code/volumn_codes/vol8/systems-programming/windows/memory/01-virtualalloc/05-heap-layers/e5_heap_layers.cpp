// e5:层次图 —— CRT malloc → 堆管理器(HeapAlloc)→ VirtualAlloc
// 剧本:
//   步骤0  堆句柄族:进程里有几个堆(建私有堆前/后各数一次)+ HeapCreate 另开一块
//   步骤1  malloc(32) 与 HeapAlloc(GetProcessHeap(),0,32) 落在同一个堆段(同 AllocBase)
//          —— CRT 的 malloc 就坐在进程堆上;私有堆的块在另一段
//   步骤2  尺寸阶梯:HeapAlloc 从 4KiB 走到 2MiB(在 0x60000 与 0x7f000 之间加密),
//          逐笔看 AllocBase:小块落主堆段;跨过某道坎后离开主段、拿新的专属段
//          (注意:返回指针 ≠ 段基址,前头有堆自己的记账头,差值就是偏移)
//   步骤3  释放后三种下场:段照旧 COMMIT(库存回 bins)/ 段保留但页被退订(RESERVE)
//          / 整段归还(FREE)—— 每一态都能在 VirtualAlloc 那层找到对应操作
//   步骤4  malloc 大块同一条链:专属段、free 后整段 FREE
#define WIN32_LEAN_AND_MEAN
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
static void* g_main_seg = nullptr; // 步骤1 里认下的主堆段基址

static void vq_row(const char* tag, void* p, SIZE_T req) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) {
        printf("  %-22s VQ 失败\n", tag);
        return;
    }
    const char* where = (mbi.State == MEM_FREE)              ? "整段已归还"
                        : (mbi.AllocationBase == g_main_seg) ? "主堆段内"
                                                             : "新段(专属)";
    printf(
        "  %-16s req=%#010llx p=%p VQ: %-7s RegionSize=%#010llx AllocBase=%p 块偏移=%#06llx %s\n",
        tag, (unsigned long long)req, p, state_str(mbi.State), (unsigned long long)mbi.RegionSize,
        mbi.AllocationBase, (unsigned long long)((char*)p - (char*)mbi.AllocationBase), where);
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== 步骤0:堆句柄族 ==\n");
    HANDLE hs[8];
    DWORD nh0 = GetProcessHeaps(8, hs); // 建私有堆之前先数一遍
    HANDLE proc_heap = GetProcessHeap();
    HANDLE priv = HeapCreate(0, 0, 0);
    DWORD nh1 = GetProcessHeaps(8, hs);
    printf("  GetProcessHeap()=%p  建私有堆前 GetProcessHeaps=%lu 个,建后 %lu 个(默认堆 + "
           "CRT/启动链的 + 我们的=%p)\n",
           proc_heap, nh0, nh1, priv);

    printf("\n== 步骤1:小块三家同台(32 字节)==\n");
    void* m = malloc(32);
    void* h = HeapAlloc(proc_heap, 0, 32);
    void* hp = HeapAlloc(priv, 0, 32);
    MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery(m, &mbi, sizeof(mbi));
    g_main_seg = mbi.AllocationBase; // 先认下主堆段,再打印分类才准
    vq_row("malloc(32)", m, 32);
    vq_row("HeapAlloc(默认堆)", h, 32);
    vq_row("HeapAlloc(私有堆)", hp, 32);
    printf("  (malloc 与默认堆 HeapAlloc 同一个 AllocBase=%p —— CRT malloc 坐在进程堆上;"
           "私有堆另起炉灶)\n",
           g_main_seg);

    printf("\n== 步骤2:尺寸阶梯(默认堆,全部留到步骤3 一起释放)==\n");
    const SIZE_T sizes[] = {0x1000,  0x10000, 0x40000, 0x60000, 0x68000, 0x78000,  0x80000,
                            0xc0000, 0xe0000, 0xf0000, 0xf8000, 0xfe000, 0x100000, 0x200000};
    void* keep[24];
    int nkeep = 0;
    for (SIZE_T s : sizes) {
        void* p = HeapAlloc(proc_heap, 0, s);
        if (!p) {
            printf("  %#llx 失败\n", (unsigned long long)s);
            continue;
        }
        char tag[32];
        snprintf(tag, sizeof(tag), "HeapAlloc(%lluK)", (unsigned long long)(s >> 10));
        vq_row(tag, p, s);
        keep[nkeep++] = p;
    }

    printf("\n== 步骤3:释放后的三种下场 ==\n");
    for (int i = 0; i < nkeep; i++) {
        HeapFree(proc_heap, 0, keep[i]);
        char tag[32];
        snprintf(tag, sizeof(tag), "free(%lluK) 后", (unsigned long long)(sizes[i] >> 10));
        vq_row(tag, keep[i], sizes[i]);
    }

    printf("\n== 步骤4:CRT malloc 的大块同一条链 ==\n");
    void* big = malloc(0x100000);
    uintptr_t big_addr = (uintptr_t)big; // 先存地址:free 后 VQ 只查地址空间,不解引用
    vq_row("malloc(1MiB)", big, 0x100000);
    free(big);
    vq_row("free 后", (void*)big_addr, 0x100000);

    free(m);
    HeapFree(proc_heap, 0, h);
    HeapFree(priv, 0, hp);
    HeapDestroy(priv);
    printf("\n结论:块小走主堆段;跨过坎(本机在 0x60000 与 0x7f000 之间,见上表)后堆管理器"
           "直接向 VirtualAlloc 要新段;释放时小段保留退订、大段整块归还 —— "
           "malloc/HeapAlloc/VirtualAlloc 三层只是记账单位不同。\n");
    return 0;
}
