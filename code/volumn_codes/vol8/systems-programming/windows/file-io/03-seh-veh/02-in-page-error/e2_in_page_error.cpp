// e2:EXCEPTION_IN_PAGE_ERROR(0xC0000006)—— 「Windows 对 mmap SIGBUS 的分叉答案」
//
// 预期剧本(来自 POSIX 对照):映射一个文件,把底层文件截短,再摸已不在文件里的区域,
// 应当拿到 IN_PAGE_ERROR 而不是 ACCESS_VIOLATION,info[2] 里还塞着 NTSTATUS。
// 实测剧本(Win11 26200 / NTFS,本机):
//   1. 只要映射活着,三条截短路全部被系统拦下(ERROR_USER_MAPPED_FILE=1224)——
//      Linux「ftruncate 砍半 + 访问 → SIGBUS」的触发器在 Windows 上根本造不出来;
//   2. 先解除映射并关掉映射对象句柄,截短才成功;
//   3. 在「文件只剩 1 页」的文件上再建 3 页映射,读第 1/2 页:不炸,零填充;
//      (Linux 上访问超出文件尾的映射页是 SIGBUS;这里静默给 0)
//   4. 0xC0000006 真正的来源是分页 I/O 真失败:网络断连/介质弹出/磁盘错误。
//      本机无可移动介质、无管理员权限建回环共享,无法安全复现 —— 本程序的
//      SEH filter 全程待命,一次都没触发,这个「没触发」本身就是实验结论。
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <excpt.h>
#include <windows.h>

static const char* ntstatus_name(DWORD s) {
    switch (s) {
        case 0xC0000011:
            return "STATUS_END_OF_FILE";
        case 0xC0000071:
            return "STATUS_SECTION_PROTECTION";
        default:
            return "";
    }
}

extern "C" __attribute__((used)) EXCEPTION_DISPOSITION page_filter(void* ep, void*, PCONTEXT,
                                                                   void*) {
    PEXCEPTION_POINTERS pointers = (PEXCEPTION_POINTERS)ep;
    PEXCEPTION_RECORD r = pointers->ExceptionRecord;
    printf("  [filter 触发] code=0x%08lX flags=0x%08lX\n", (unsigned long)r->ExceptionCode,
           (unsigned long)r->ExceptionFlags);
    if (r->ExceptionCode == 0xC0000006)
        printf("    --> EXCEPTION_IN_PAGE_ERROR  info[2](NTSTATUS)=0x%08lX %s\n",
               (unsigned long)r->ExceptionInformation[2],
               ntstatus_name((DWORD)r->ExceptionInformation[2]));
    else
        printf("    info[0]=%llu info[1]=0x%llX\n", (unsigned long long)r->ExceptionInformation[0],
               (unsigned long long)r->ExceptionInformation[1]);
    return (EXCEPTION_DISPOSITION)1;
}

// 惯用法:受保护体内不能 return/goto 跳出(asm 会变不可达),用 faulted 标志分岔
__attribute__((noinline)) void probe_read(volatile char* p, int* result) {
    int faulted = 1;
    __try1(page_filter)* result = *p;
    faulted = 0;
    __except1 if (faulted)* result = -1;
}

static const DWORD PAGE_SZ = 4096;

static void report_size(const char* tag, HANDLE h) {
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    printf("  [%s] file size = %lld\n", tag, sz.QuadPart);
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    char path[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, path);
    lstrcpyA(path + n, "seh_veh_inpage.bin");
    printf("file=%s\n", path);

    // 无缓冲写入 3 页:数据落盘但不进缓存,排除「缓存里还有旧页」的干扰
    HANDLE hw = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            CREATE_ALWAYS, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    static unsigned char buf[3 * PAGE_SZ] __attribute__((aligned(512)));
    for (unsigned i = 0; i < sizeof(buf); ++i)
        buf[i] = (unsigned char)(0x40 + (i & 0x3f));
    DWORD w = 0;
    WriteFile(hw, buf, sizeof(buf), &w, NULL);
    CloseHandle(hw);
    printf("file written unbuffered: %lu bytes(内容 0x40..0x7f 循环)\n", (unsigned long)w);

    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    report_size("写完", h);

    // ── 第一幕:映射活着,截短被拦 ─────────────────────────────
    HANDLE map = CreateFileMappingA(h, NULL, PAGE_READWRITE, 0, 3 * PAGE_SZ, NULL);
    char* view = (char*)MapViewOfFile(map, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    printf("view=%p(3 页映射建立)\n", view);

    LARGE_INTEGER cut;
    cut.QuadPart = PAGE_SZ;
    SetFilePointerEx(h, cut, NULL, FILE_BEGIN);
    BOOL ok = SetEndOfFile(h);
    printf("  映射存活时 SetEndOfFile(4096)      -> %d gle=%lu (1224=ERROR_USER_MAPPED_FILE)\n", ok,
           GetLastError());
    FILE_ALLOCATION_INFO ai{PAGE_SZ};
    ok = SetFileInformationByHandle(h, FileAllocationInfo, &ai, sizeof(ai));
    printf("  映射存活时 FileAllocationInfo(4096) -> %d gle=%lu\n", ok, GetLastError());

    printf("== 读 view[4096+5](此刻文件其实还是 3 页,应得 0x45)\n");
    int result = 0;
    probe_read(view + PAGE_SZ + 5, &result);
    printf("-> 0x%02x\n", result & 0xff);

    // ── 第二幕:解除映射 + 关掉映射对象句柄,截短才放行 ─────────
    UnmapViewOfFile(view);
    CloseHandle(map);
    SetFilePointerEx(h, cut, NULL, FILE_BEGIN);
    ok = SetEndOfFile(h);
    printf("  解除映射+关句柄后 SetEndOfFile      -> %d\n", ok);
    report_size("截短后", h);

    // ── 第三幕:在 1 页文件上再建 3 页映射,读「文件里不存在」的页 ─
    map = CreateFileMappingA(h, NULL, PAGE_READWRITE, 0, 3 * PAGE_SZ, NULL);
    view = (char*)MapViewOfFile(map, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    printf("view=%p(3 页映射盖在 1 页文件上)\n", view);

    printf("== 读 view[5](第 0 页,文件里有)\n");
    probe_read(view + 5, &result);
    printf("-> 0x%02x\n", result & 0xff);

    printf("== 读 view[1*4096+5](第 1 页,文件里没有)\n");
    probe_read(view + PAGE_SZ + 5, &result);
    printf("-> %d(0=零填充,无异常;Linux 这里是 SIGBUS)\n", result);

    printf("== 读 view[2*4096](第 2 页,文件里没有)\n");
    probe_read(view + 2 * PAGE_SZ, &result);
    printf("-> %d\n", result);

    printf("== 读 view[3*4096](越过映射边界本身)\n");
    probe_read(view + 3 * PAGE_SZ, &result);
    printf("-> %d(-1=异常;这才是 ACCESS_VIOLATION 的地界)\n", result);

    UnmapViewOfFile(view);
    CloseHandle(map);
    CloseHandle(h);
    DeleteFileA(path);
    printf("done\n");
    return 0;
}
