// e3b_nobuffering_stale.cpp —— share 全开也不是什么都同步:缓冲写 + 非缓冲读,试着测脏读
//
// 编译:
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e3b_nobuffering_stale.cpp -o
//   e3b_nobuffering_stale.exe
// 运行:
//   chmod +x e3b_nobuffering_stale.exe && ./e3b_nobuffering_stale.exe
//
// 背景:两个普通(缓冲)句柄之间,写完另一个立刻读到 —— 缓存管理器保证一致。
// 但把读端换成 FILE_FLAG_NO_BUFFERING(绕开缓存直读盘),写端的脏页还躺在缓存里
// 没落盘时,理论上直读撞上的是盘上旧数据。官方口径(File Buffering 文档):
// 缓冲与非缓冲混用时,切换方向前要 FlushFileBuffers,否则读到什么没有承诺。
//
// 本实验把两种排列都摆出来:
//   [1] 基线:盘上是 0xAA,非缓冲读 -> AA(此刻盘缓存一致)
//   [2] 排列一:缓冲写 0xBB 之后,新开一个非缓冲句柄再读(开门时系统有机会对齐)
//   [3] 排列二:非缓冲读句柄先开好、跨过缓冲写保持打开、写完在旧句柄上读
//       (没有"开门对齐"的机会,理论上最容易脏读)
//   [4] FlushFileBuffers(写句柄)之后,必然读到 BB
//
// 本机实测(Win11 26200 / NTFS / NVMe):两种排列都读到新鲜 BB,脏读没有复现。
// 另外试过 32MiB 整段缓冲写完立刻读尾页(写懒性追赶最吃紧的形态)也一样是 BB。
// 结论如实记:文档的警告仍在,混用前 FlushFileBuffers 是正路;但在这台快盘
// 机器上,三种加码都没能把旧数据钓出来 —— "读到什么看缓存"的下场,本机没看着。

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <string>

static std::wstring make_path(const wchar_t* name) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"sysprog-supp-e3";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\" + name;
}

static HANDLE open_direct_reader(const std::wstring& path) {
    return CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, nullptr);
}

static void show8(const unsigned char* p) {
    for (int i = 0; i < 8; i++) {
        printf("%02X ", p[i]);
    }
}

int main() {
    std::wstring path = make_path(L"coher.bin");
    DeleteFileW(path.c_str());

    // 造 8KiB 的 0xAA,用非缓冲写直接铺到盘上
    unsigned char* abuf = (unsigned char*)_aligned_malloc(4096, 4096);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, nullptr);
    memset(abuf, 0xAA, 4096);
    DWORD w = 0;
    WriteFile(h, abuf, 4096, &w, nullptr);
    WriteFile(h, abuf, 4096, &w, nullptr);
    CloseHandle(h);

    printf("[1] 基线:盘上铺满 0xAA\n");
    HANDLE hr = open_direct_reader(path);
    DWORD got = 0;
    ReadFile(hr, abuf, 4096, &got, nullptr);
    printf("    NO_BUFFERING 读 4096B -> %lu 字节,首 8 字节 = ", got);
    show8(abuf);
    printf(" <- AA,一致\n");
    CloseHandle(hr);

    printf("[2] 排列一:缓冲写 0xBB 后新开非缓冲句柄再读\n");
    HANDLE hb = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    memset(abuf, 0xBB, 4096);
    WriteFile(hb, abuf, 4096, &w, nullptr);
    printf("    WriteFile(缓冲) 写 4096B -> %lu,返回即\"已进缓存\"\n", w);
    hr = open_direct_reader(path);
    ReadFile(hr, abuf, 4096, &got, nullptr);
    printf("    新开的 NO_BUFFERING 读 -> %lu 字节,首 8 字节 = ", got);
    show8(abuf);
    printf(" <- %s\n", abuf[0] == 0xBB ? "BB,新鲜" : "AA,脏读复现!");
    CloseHandle(hr);

    printf("[3] 排列二:非缓冲读句柄先开好,跨过缓冲写在旧句柄上读\n");
    // (盘上此刻可能还是 AA、缓存里是 BB —— 排列一若没把 BB 压到盘上,这里就有得看)
    hr = open_direct_reader(path); // 先开好,后面不再重新开门
    LARGE_INTEGER z;
    z.QuadPart = 0;
    SetFilePointerEx(hb, z, nullptr, FILE_BEGIN); // hb 指针回到 0,覆写同一页
    memset(abuf, 0xBB, 4096);
    WriteFile(hb, abuf, 4096, &w, nullptr); // 缓冲写,脏页进缓存
    printf("    缓冲写 0xBB(读句柄全程开着,没有重新开门的机会)\n");
    ReadFile(hr, abuf, 4096, &got, nullptr);
    printf("    旧 NO_BUFFERING 句柄读 -> %lu 字节,首 8 字节 = ", got);
    show8(abuf);
    printf(" <- %s\n", abuf[0] == 0xBB ? "BB,新鲜" : "AA,脏读复现!");
    CloseHandle(hr);

    printf("[4] FlushFileBuffers(写句柄)后复读\n");
    FlushFileBuffers(hb);
    CloseHandle(hb);
    hr = open_direct_reader(path);
    ReadFile(hr, abuf, 4096, &got, nullptr);
    printf("    NO_BUFFERING 读 -> %lu 字节,首 8 字节 = ", got);
    show8(abuf);
    printf(" <- 刷盘后必然 BB\n");
    CloseHandle(hr);

    _aligned_free(abuf);
    DeleteFileW(path.c_str());
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    RemoveDirectoryW((std::wstring(tmp) + L"sysprog-supp-e3").c_str());
    printf("\n收尾:临时文件与目录已清理\n");
    return 0;
}
