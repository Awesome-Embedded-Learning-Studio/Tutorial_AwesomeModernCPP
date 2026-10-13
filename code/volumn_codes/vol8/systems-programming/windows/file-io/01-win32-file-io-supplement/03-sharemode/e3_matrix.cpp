// e3_matrix.cpp —— dwShareMode 全矩阵:独占、单向共享、双向检查、FILE_SHARE_DELETE、
// 同进程约束、指针独立,最后跨进程复验
//
// 编译:
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e3_matrix.cpp -o e3_matrix.exe
// 运行:
//   chmod +x e3_matrix.exe && ./e3_matrix.exe
//
// 观察点:
//   [1] 矩阵:第一个句柄的 share 组合 x 第二个句柄要的访问 -> 成功句柄 / 拒绝码
//       规则(内核按"文件对象"逐个比对,与进程/线程无关):
//         新句柄要的访问 ⊆ 每个在场句柄的 share
//         在场句柄的访问 ⊆ 新句柄的 share
//   [2] 双向检查:第一个句柄自己在写,第二个句柄 share=0 就进不来 —— share 不是
//       "我能开别人",是"我允许别人对我做什么";两边都得点头
//   [3] 删除/改名:没有 FILE_SHARE_DELETE 时 DeleteFileW/MoveFileExW 双双被拒;
//       有 D 时 DeleteFileW 成功 -> 文件进入 delete pending:句柄还能读写,
//       新开句柄被拒,最后一个句柄关掉才真消失(POSIX unlink 语义的 Windows 近亲)
//   [4] 同进程约束:以上全部发生在同一个进程里 —— POSIX 的 open() 没有这层检查
//   [5] 指针独立 + 数据可见:两个句柄都能开时,一个的偏移推不动另一个;
//       普通缓冲句柄之间缓存一致,写完另一个立刻读到
//   [6] 跨进程复验:真开一个子进程,独占句柄挂父进程手里,子进程撞 32;
//       父进程放手后子进程再开就成功
//
// 子进程协议:e3_matrix.exe --child <goEvt> <doneEvt> <path>
//   两个 auto-reset 事件做两轮握手:go=父进程允许尝试,done=子进程已打印完结果

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <shellapi.h> // CommandLineToArgvW
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

// ---------- 工具 ----------

static std::wstring tmp_dir() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"sysprog-supp-e3";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static std::wstring fresh_file(const wchar_t* name, const char* content) {
    std::wstring path = tmp_dir() + L"\\" + name;
    DeleteFileW(path.c_str());
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(h, content, (DWORD)strlen(content), &w, nullptr);
        CloseHandle(h);
    }
    return path;
}

static const char* share_name(DWORD s) {
    switch (s) {
        case 0:
            return "0(excl)";
        case FILE_SHARE_READ:
            return "R";
        case FILE_SHARE_WRITE:
            return "W";
        case FILE_SHARE_READ | FILE_SHARE_WRITE:
            return "R|W";
        case FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE:
            return "R|W|D";
        default:
            return "?";
    }
}

static const char* acc_name(DWORD a) {
    if (a == GENERIC_READ) {
        return "READ      ";
    }
    if (a == GENERIC_WRITE) {
        return "WRITE     ";
    }
    if (a == (GENERIC_READ | GENERIC_WRITE)) {
        return "READ|WRITE";
    }
    if (a == DELETE) {
        return "DELETE    ";
    }
    return "?         ";
}

// 尝试第二个句柄,打印一行结果;成功则关掉
static void try_open(const std::wstring& path, DWORD access, DWORD share, const char* tag) {
    SetLastError(0);
    HANDLE h = CreateFileW(path.c_str(), access, share, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD err = GetLastError();
    if (h != INVALID_HANDLE_VALUE) {
        printf("    %-22s 第二个访问=%s -> 开到 h=%p\n", tag, acc_name(access), h);
        CloseHandle(h);
    } else {
        printf("    %-22s 第二个访问=%s -> 拒绝 err=%lu%s\n", tag, acc_name(access), err,
               err == 32 ? "(ERROR_SHARING_VIOLATION)" : "");
    }
}

static long long pos(HANDLE h) {
    LARGE_INTEGER d, got;
    d.QuadPart = 0;
    if (!SetFilePointerEx(h, d, &got, FILE_CURRENT)) {
        return -1;
    }
    return got.QuadPart;
}

// ---------- 子进程模式 ----------

static int child_main(const wchar_t* go_name, const wchar_t* done_name, const wchar_t* path) {
    HANDLE go = CreateEventW(nullptr, FALSE, FALSE, go_name);
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, done_name);
    if (!go || !done) {
        return 1;
    }

    for (int round = 1; round <= 2; round++) {
        WaitForSingleObject(go, INFINITE);
        SetLastError(0);
        HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD err = GetLastError();
        if (h != INVALID_HANDLE_VALUE) {
            printf("    [子进程 pid=%lu 第%d轮] CreateFileW(READ) -> h=%p 成功\n",
                   GetCurrentProcessId(), round, h);
            fflush(stdout);
            SetEvent(done);
            CloseHandle(h); // 关完再退出;父进程不依赖关的时机
        } else {
            printf("    [子进程 pid=%lu 第%d轮] CreateFileW(READ) -> 拒绝 err=%lu\n",
                   GetCurrentProcessId(), round, err);
            fflush(stdout);
            SetEvent(done);
        }
    }
    return 0;
}

// ---------- 主流程 ----------

int main() {
    // 管道捕获下 stdout 全缓冲,父子两进程的行会乱序;直接不缓冲
    setvbuf(stdout, nullptr, _IONBF, 0);
    int argcW = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argcW);
    if (argcW == 5 && wcscmp(argv[1], L"--child") == 0) {
        int rc = child_main(argv[2], argv[3], argv[4]);
        LocalFree(argv);
        return rc;
    }
    LocalFree(argv);

    printf("== [1] 矩阵:第一个句柄 share x 第二个句柄要的访问 ==\n");
    printf("    (0(excl)=独占;第一个句柄固定 GENERIC_READ|GENERIC_WRITE;第二个句柄 share\n");
    printf("     给足 R|W|D,只测\"第二访问 vs 第一 share\"这一向;双向检查见 [2])\n");
    printf("    %-8s %-11s %-11s %-11s %s\n", "第一share", "要READ", "要WRITE", "要R|W",
           "要DELETE");

    const DWORD seconds[] = {GENERIC_READ, GENERIC_WRITE, GENERIC_READ | GENERIC_WRITE, DELETE};
    const DWORD firsts[] = {0, FILE_SHARE_READ, FILE_SHARE_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE};
    for (DWORD fs : firsts) {
        std::wstring path = fresh_file(L"matrix.bin", "MATRIX-DEMO");
        HANDLE h1 = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, fs, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        printf("    %-8s", share_name(fs));
        for (DWORD sa : seconds) {
            SetLastError(0);
            HANDLE h2 = CreateFileW(path.c_str(), sa,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD err = GetLastError();
            if (h2 != INVALID_HANDLE_VALUE) {
                printf("  %-11s", "成功");
                CloseHandle(h2);
            } else {
                char cell[24];
                snprintf(cell, sizeof(cell), "拒%lu", err);
                printf("  %-11s", cell);
            }
        }
        printf("\n");
        CloseHandle(h1);
        DeleteFileW(path.c_str());
    }
    printf("    拒32 = ERROR_SHARING_VIOLATION。要点:share 位与访问位是两把尺子,\n");
    printf("    要 DELETE 访问,在场句柄必须给了 FILE_SHARE_DELETE\n");

    printf("\n== [2] 双向检查:第二个句柄的 share 也要装得下第一个句柄的在用访问 ==\n");
    printf("    (第一句柄:访问 R|W、share R|W;第二句柄只要 READ,但 share 各配一档)\n");
    {
        std::wstring path = fresh_file(L"bidir.bin", "BIDIR");
        HANDLE h1 = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        try_open(path, GENERIC_READ, 0, "第二 share=0(独占)");
        try_open(path, GENERIC_READ, FILE_SHARE_READ, "第二 share=R      ");
        try_open(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, "第二 share=R|W    ");
        CloseHandle(h1);
        DeleteFileW(path.c_str());
    }
    printf("    第一个句柄自己握着写访问,第二个句柄 share=0 或只给 R,都装不下它,\n");
    printf("    于是被拒 —— share 声明的是\"我允许别人怎么动这个文件\",不是\"我能开门\"\n");

    printf("\n== [3] 删除/改名:FILE_SHARE_DELETE 的有无 ==\n");
    // a) 没有 D
    {
        std::wstring path = fresh_file(L"del_no_d.bin", "DELETE-ME");
        std::wstring ren = tmp_dir() + L"\\del_no_d_renamed.bin";
        DeleteFileW(ren.c_str());
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        SetLastError(0);
        BOOL dr = DeleteFileW(path.c_str());
        DWORD de = GetLastError();
        SetLastError(0);
        BOOL mr = MoveFileExW(path.c_str(), ren.c_str(), 0);
        DWORD me = GetLastError();
        printf("    (a) share=R|W(无D)句柄在握:\n");
        printf("        DeleteFileW -> ret=%d err=%lu%s\n", dr, de,
               de == 5 ? "(ERROR_ACCESS_DENIED)" : "");
        printf("        MoveFileExW(改名) -> ret=%d err=%lu%s\n", mr, me,
               me == 5 ? "(ERROR_ACCESS_DENIED)" : "");
        CloseHandle(h);
        SetLastError(0);
        mr = MoveFileExW(path.c_str(), ren.c_str(), 0);
        printf("        句柄关闭后再改名 -> ret=%d err=%lu  (放手才放行)\n", mr, GetLastError());
        DeleteFileW(ren.c_str());
        DeleteFileW(path.c_str());
    }
    // b) 有 D
    {
        std::wstring path = fresh_file(L"del_with_d.bin", "DELETE-ME");
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        printf("    (b) share=R|W|D 句柄在握:\n");
        SetLastError(0);
        BOOL dr = DeleteFileW(path.c_str());
        printf("        DeleteFileW -> ret=%d err=%lu\n", dr, GetLastError());

        FILE_STANDARD_INFO si{};
        GetFileInformationByHandleEx(h, FileStandardInfo, &si, sizeof(si));
        printf("        DeletePending -> %d  (FileStandardInfo 直读)\n", (int)si.DeletePending);

        // 句柄还能读写吗
        char buf[16];
        DWORD got = 0;
        SetLastError(0);
        BOOL rr = ReadFile(h, buf, sizeof(buf), &got, nullptr);
        printf("        ReadFile -> ret=%d 读到=%lu 前8字节=%02X%02X%02X%02X%02X%02X%02X%02X\n", rr,
               got, (unsigned char)buf[0], (unsigned char)buf[1], (unsigned char)buf[2],
               (unsigned char)buf[3], (unsigned char)buf[4], (unsigned char)buf[5],
               (unsigned char)buf[6], (unsigned char)buf[7]);
        LARGE_INTEGER d;
        d.QuadPart = 0;
        SetFilePointerEx(h, d, nullptr, FILE_BEGIN);
        DWORD put = 0;
        SetLastError(0);
        BOOL wr = WriteFile(h, "after-del", 9, &put, nullptr);
        printf("        WriteFile -> ret=%d 写到=%lu  (delete pending 下句柄照常干活)\n", wr, put);

        // 外部视角
        SetLastError(0);
        DWORD attr = GetFileAttributesW(path.c_str());
        printf("        GetFileAttributesW -> 0x%08lX err=%lu\n", attr, GetLastError());
        SetLastError(0);
        HANDLE probe = CreateFileW(path.c_str(), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        printf("        新开句柄(OPEN_EXISTING) -> %s err=%lu\n",
               probe != INVALID_HANDLE_VALUE ? "成功" : "拒绝", GetLastError());
        if (probe != INVALID_HANDLE_VALUE) {
            CloseHandle(probe);
        }

        CloseHandle(h);
        attr = GetFileAttributesW(path.c_str());
        printf("        最后一个句柄关闭后 GetFileAttributesW -> 0x%08lX(0xFFFFFFFF=没了)\n", attr);
        printf("        (注意 err=2:本机 Win11 26200 的 DeleteFileW 走的是\"名字立刻摘掉、\n");
        printf("         句柄吊命到最后一关\"的近 POSIX 语义;新开句柄看到的是 FILE_NOT_FOUND,\n");
        printf("         不是老资料里的 ACCESS_DENIED)\n");
    }
    // c) 有 D 时改名
    {
        std::wstring path = fresh_file(L"ren_d.bin", "RENAME-ME");
        std::wstring ren = tmp_dir() + L"\\ren_d_new.bin";
        DeleteFileW(ren.c_str());
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        SetLastError(0);
        BOOL mr = MoveFileExW(path.c_str(), ren.c_str(), 0);
        DWORD me = GetLastError();
        printf("    (c) share=R|W|D 句柄在握时改名 -> ret=%d err=%lu\n", mr, me);
        char buf[16];
        DWORD got = 0;
        SetLastError(0);
        BOOL rr = ReadFile(h, buf, sizeof(buf), &got, nullptr);
        printf("        改名后原句柄 ReadFile -> ret=%d 读到=%lu \"%.*s\"(句柄跟文件走)\n", rr, got,
               (int)(got < 9 ? got : 9), buf);
        CloseHandle(h);
        DWORD attr = GetFileAttributesW(ren.c_str());
        printf("        关句柄后新名字健在 -> attr=0x%08lX(0xFFFFFFFF=没跟过去)\n", attr);
        DeleteFileW(ren.c_str());
        DeleteFileW(path.c_str());
    }

    printf("\n== [4] 同进程约束 ==\n");
    printf("    [1]-[3] 的\"第二个句柄\"全部开在同一个进程里:Windows 的共享检查挂在\n");
    printf("    内核文件对象上,不看出身;POSIX 的 open() 对同一进程再开一次没有任何约束\n");

    printf("\n== [5] 指针独立 + 数据可见(share=R|W 双开) ==\n");
    {
        std::wstring path = fresh_file(L"ptr.bin", "V1");
        HANDLE h1 = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE h2 = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD put = 0;
        WriteFile(h1, "V2", 2, &put, nullptr); // h1 指针本来就在 EOF(=2),原地覆写
        printf("    h1 覆写 \"V2\" 后:pos(h1)=%lld pos(h2)=%lld  <- h2 偏移纹丝不动\n", pos(h1),
               pos(h2));
        char buf[3] = {};
        DWORD got = 0;
        ReadFile(h2, buf, 2, &got, nullptr);
        printf("    h2 从自己的 0 位置读 -> \"%s\"(缓冲句柄间缓存一致,写完立刻可见)\n", buf);
        CloseHandle(h1);
        CloseHandle(h2);
        DeleteFileW(path.c_str());
    }

    printf("\n== [6] 跨进程复验(自重挂子进程) ==\n");
    {
        std::wstring path = fresh_file(L"xproc.bin", "XPROC");
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);

        std::wstring go_name = L"Local\\supp-e3-go-" + std::to_wstring(GetCurrentProcessId());
        std::wstring done_name = L"Local\\supp-e3-done-" + std::to_wstring(GetCurrentProcessId());
        HANDLE go = CreateEventW(nullptr, FALSE, FALSE, go_name.c_str());
        HANDLE done = CreateEventW(nullptr, FALSE, FALSE, done_name.c_str());

        std::wstring cmd = std::wstring(L"\"") + exe + L"\" --child \"" + go_name + L"\" \"" +
                           done_name + L"\" \"" + path + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        // 子进程继承本控制台,printf 直接汇合到同一输出流
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                            &pi)) {
            printf("    CreateProcessW 失败 err=%lu\n", GetLastError());
        } else {
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
            printf("    父进程 pid=%lu 以 share=0 独占句柄 h=%p 挂住文件\n", GetCurrentProcessId(),
                   h);
            SetEvent(go);                        // 允许子进程第 1 轮尝试
            WaitForSingleObject(done, INFINITE); // 子进程已打印第 1 轮结果
            CloseHandle(h);                      // 放手
            SetEvent(go);                        // 允许第 2 轮
            WaitForSingleObject(done, INFINITE);
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        CloseHandle(go);
        CloseHandle(done);
        DeleteFileW(path.c_str());
    }

    RemoveDirectoryW(tmp_dir().c_str());
    printf("\n收尾:临时目录已清理\n");
    return 0;
}
