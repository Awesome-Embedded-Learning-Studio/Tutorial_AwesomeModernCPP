// e1_lifecycle.cpp —— E1 命名对象的创建/打开/撞名与句柄计数生命周期
//
// 编译(WSL 里以相对路径调 MSYS2 UCRT64 g++,cwd 必须在 WSL 文件系统上):
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e1_lifecycle.cpp -o e1_lifecycle.exe
// 运行:
//   chmod +x e1_lifecycle.exe && ./e1_lifecycle.exe
//
// 观察点:
//   (1) 创建即打开的合一语义:两次 CreateFileMappingW 同名,第二次句柄照常有效,
//       但 GetLastError()=183(ERROR_ALREADY_EXISTS)——"创建返回的是不是新建的"要看它
//   (2) 同名不同尺寸:第二次 Create 传 1MiB 请求也返回 183 + 有效句柄,但对象还是
//       64KiB 那个(VirtualQuery 的 RegionSize 作证)——尺寸参数被静默忽略
//   (3) OpenFileMappingW:对不存在的名字 → NULL + err=2(ERROR_FILE_NOT_FOUND)
//   (4) 生命周期两段论:名字与对象分头死——名字死于最后一个"句柄"关闭(实测句柄全关后
//       OpenFileMappingW 立刻 err=2);对象死于最后一个"引用"撒手(视图也是引用,句柄全关
//       后视图照常读写);没有显式 unlink,句柄+视图引用计数归零就是全部
//   (5) 名字空间:Global\ 前缀的页文件后备 section 在非特权进程里 → NULL + err=5
//       (SeCreateGlobalPrivilege);同前缀的命名互斥体却创建成功——特权门槛只拦 section
//   (6) SEC_RESERVE 页文件后备(正主):保留态 MEM_RESERVE/Protect=0,VirtualAlloc
//       提交后可写;W02 见过真文件后备会把"保留"静默兑现成"提交",页文件后备不会

#include "../common/shm_util.hpp"

static DWORD hc() {
    DWORD n = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &n)) {
        n = (DWORD)-1;
    }
    return n;
}

static const char* state_name(SIZE_T s) {
    switch (s) {
        case MEM_COMMIT:
            return "MEM_COMMIT";
        case MEM_RESERVE:
            return "MEM_RESERVE";
        case MEM_FREE:
            return "MEM_FREE";
        default:
            return "(other)";
    }
}

int main() {
    const DWORD pid = GetCurrentProcessId();
    const std::wstring name = local_name(L"E1", pid);       // Local\SysProgShm_E1_<pid>
    const std::wstring ghost = local_name(L"E1ghost", pid); // 保证不存在
    printf("[E1] 命名对象生命周期,pid=%lu,名字=%ls\n", pid, name.c_str());
    fflush(stdout);

    // ---------- (1)(2) 创建即打开:撞名与撞尺寸 ----------
    printf("\n[1] CreateFileMappingW 两次同名 + OpenFileMappingW(括号里是进程句柄总数)\n");
    DWORD base_cnt = hc();
    SetLastError(0);
    HANDLE h1 = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 64 * 1024,
                                   name.c_str());
    printf("  第1次 Create(请求 64KiB)  -> h=%p err=%lu  (handles %lu->%lu)\n", h1, GetLastError(),
           base_cnt, hc());
    SetLastError(0);
    HANDLE h2 = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 64 * 1024,
                                   name.c_str());
    printf("  第2次 Create(同名)       -> h=%p err=%lu  (183=ERROR_ALREADY_EXISTS,句柄照常有效)\n",
           h2, GetLastError());
    SetLastError(0);
    HANDLE h2big = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 1024 * 1024,
                                      name.c_str());
    void* vbig = MapViewOfFile(h2big, FILE_MAP_READ, 0, 0, 0);
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(vbig, &mbi, sizeof mbi);
    printf("  第3次 Create(同名,请求 1MiB) -> h=%p err=%lu,映射后 RegionSize=0x%zX(%zuKiB)"
           " <- 尺寸被忽略,还是 64KiB 那个对象\n",
           h2big, GetLastError(), mbi.RegionSize, mbi.RegionSize / 1024);
    if (vbig) {
        UnmapViewOfFile(vbig);
    }
    CloseHandle(h2big);
    DWORD before_h3 = hc();
    SetLastError(0);
    HANDLE h3 = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    printf("  OpenFileMappingW(存在)   -> h=%p err=%lu  (handles %lu->%lu)\n", h3, GetLastError(),
           before_h3, hc());
    SetLastError(0);
    HANDLE ho = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, ghost.c_str());
    printf("  OpenFileMappingW(不存在) -> h=%p err=%lu  (2=ERROR_FILE_NOT_FOUND)\n", ho,
           GetLastError());

    // ---------- (4) 句柄计数生命周期:名字与对象分两段死 ----------
    printf("\n[2] 生命周期两段论:名字死于最后一个句柄,对象死于最后一个引用\n");
    unsigned char* v = (unsigned char*)MapViewOfFile(h2, FILE_MAP_WRITE, 0, 0, 0);
    printf("  MapViewOfFile(h2 全景)   -> base=%p\n", v);
    CloseHandle(h1);
    SetLastError(0);
    HANDLE h4 = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    printf("  CloseHandle(h1),h2 还拿着句柄 -> 按名打开: h=%p err=%lu (有句柄在,名字就在;"
           "h4%s 刚关掉的 h1 那个句柄槽)\n",
           h4, GetLastError(), h4 == h1 ? "复用了" : "没复用");
    CloseHandle(h4);
    CloseHandle(h2);
    CloseHandle(h3);
    printf("  关掉 h2/h3:句柄数回到 %lu(基线 %lu),本进程再无该对象的句柄\n", hc(), base_cnt);
    memset(v, 0xAA, 4096);
    unsigned ok = 0;
    for (int i = 0; i < 4096; ++i) {
        ok += (v[i] == 0xAA);
    }
    printf("  句柄全关后写视图 4096 字节再读回:%u/4096 字节相符 —— 视图是另一类引用,"
           "对象还活着\n",
           ok);
    SetLastError(0);
    HANDLE h5 = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    printf("  句柄全关后按名再打开      -> h=%p err=%lu (2=ERROR_FILE_NOT_FOUND!)名字在最后一个"
           "句柄关闭那刻就摘了,对象匿名续命\n",
           h5, GetLastError());
    UnmapViewOfFile(v);
    printf("  UnmapViewOfFile:最后一根引用撒手,对象此刻才销毁(没有显式 unlink;"
           "名字已亡,销毁无从按名查证)\n");

    // ---------- (5) Local\ vs Global\ ----------
    printf("\n[3] 名字空间:Local\\ 按会话隔离,Global\\ 要特权(section 才要)\n");
    const std::wstring gname = L"Global\\SysProgShm_E1g_" + std::to_wstring(pid);
    SetLastError(0);
    HANDLE gs = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 64 * 1024,
                                   gname.c_str());
    printf("  CreateFileMappingW(Global\\...section) -> h=%p err=%lu (5=ERROR_ACCESS_DENIED,"
           "SeCreateGlobalPrivilege 未持有)\n",
           gs, GetLastError());
    if (gs) {
        CloseHandle(gs);
    }
    SetLastError(0);
    HANDLE gm = CreateMutexW(nullptr, FALSE, gname.c_str());
    printf("  CreateMutexW     (Global\\...mutex)    -> h=%p err=%lu %s\n", gm, GetLastError(),
           gm ? "(成功!特权门槛只拦 section,不拦互斥体/事件/信号量)" : "");
    if (gm) {
        CloseHandle(gm);
    }

    // ---------- (6) SEC_RESERVE 页文件后备 ----------
    printf("\n[4] SEC_RESERVE|PAGE_READWRITE(页文件后备,保留语义的正主)\n");
    SetLastError(0);
    HANDLE rs = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_RESERVE, 0,
                                   4 * 1024 * 1024, nullptr);
    unsigned char* rv = (unsigned char*)MapViewOfFile(rs, FILE_MAP_WRITE, 0, 0, 0);
    VirtualQuery(rv, &mbi, sizeof mbi);
    printf("  4MiB SEC_RESERVE 映射 -> base=%p State=%s Protect=0x%lX RegionSize=0x%zX\n", rv,
           state_name(mbi.State), (unsigned long)mbi.Protect, mbi.RegionSize);
    void* c = VirtualAlloc(rv, 4096, MEM_COMMIT, PAGE_READWRITE);
    VirtualQuery(rv, &mbi, sizeof mbi);
    printf("  VirtualAlloc(首页 MEM_COMMIT) -> %p,State=%s Protect=0x%lX\n", c,
           state_name(mbi.State), (unsigned long)mbi.Protect);
    *(uint64_t*)rv = 0x5A5A5A5A5A5A5A5Aull;
    printf("  提交后写读:*(uint64_t*)base=0x%llX\n", (unsigned long long)*(uint64_t*)rv);
    UnmapViewOfFile(rv);
    CloseHandle(rs);

    printf("\n[E1] 完:handles %lu(基线 %lu,无泄漏)\n", hc(), base_cnt);
    fflush(stdout);
    return 0;
}
