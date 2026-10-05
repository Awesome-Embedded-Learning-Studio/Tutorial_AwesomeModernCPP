// E3 子进程:四种死法各走一遍,看清理路径(atexit / stdout 缓冲 / DLL detach)哪条还活着。
// 关键布置:
//  - atexit 注册一个写 stderr 的善后函数(stderr 无缓冲,写出来就是证据)
//  - LoadLibrary 一个带 DllMain 日志的 DLL,看 loader 有没有给 detach 通知
//  - stdout 上留一条【不带换行、不 fflush】的标记:只有 CRT 正常 exit 路径会刷缓冲,
//    标记落进文件与否 = 缓冲有没有被 flush(stdout 被父进程重定向到文件,全缓冲)
// 用法: e3_child.exe <mode> <dll_path> <ready_event_name>
//   mode = normal | exitprocess | hang | ctrlbreak
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <windows.h>

static void raw_err(const char* s) {
    DWORD n;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), s, (DWORD)lstrlenA(s), &n, nullptr);
}

static void atexit_line() {
    // 不用 printf:atexit 兑现时进程已在收尾,直接裸写 stderr 最稳
    raw_err("[child] atexit handler ran (CRT exit path honored)\n");
}

static BOOL WINAPI on_ctrl(DWORD type) {
    if (type == CTRL_BREAK_EVENT) {
        raw_err("[child] CTRL_BREAK_EVENT handler: graceful cleanup ran, calling ExitProcess(5)\n");
        ExitProcess(5);
    }
    return FALSE;
}

int wmain(int argc, wchar_t** argv) {
    // 注意:stdout 故意保持默认全缓冲(重定向到文件时),一行标记只在正常 exit 时才该落盘
    if (argc != 4)
        return 1;
    const wchar_t* mode = argv[1];
    const wchar_t* dll = argv[2];
    const wchar_t* evt_name = argv[3];

    if (wcscmp(mode, L"ctrlbreak") != 0)
        atexit(atexit_line);
    HMODULE h = LoadLibraryW(dll);
    if (!h) {
        fprintf(stderr, "[child] LoadLibraryW failed err=%lu\n", GetLastError());
        return 2;
    }
    fprintf(stdout, "STDOUT_MARKER_ONLY_SURVIVES_REAL_EXIT"); // 无换行,无 fflush
    fprintf(
        stderr,
        "[child] mode=%ls setup done (atexit registered, dll loaded, marker in stdout buffer)\n",
        mode);

    HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, evt_name);
    if (ev) {
        SetEvent(ev);
        CloseHandle(ev);
    } else {
        fprintf(stderr, "[child] OpenEventW(%ls) failed err=%lu\n", evt_name, GetLastError());
    }

    if (wcscmp(mode, L"normal") == 0) {
        fprintf(stderr, "[child] returning 0 from main (CRT exit path)\n");
        return 0; // atexit -> stdout flush -> ExitProcess 内部 -> loader detach
    }
    if (wcscmp(mode, L"exitprocess") == 0) {
        fprintf(stderr, "[child] calling ExitProcess(0) DIRECTLY, bypassing CRT exit\n");
        ExitProcess(0);
    }
    if (wcscmp(mode, L"hang") == 0) {
        fprintf(stderr, "[child] idling, waiting to be TerminateProcess'd...\n");
        for (int i = 0; i < 300; i++)
            Sleep(200); // 上限 60s,防父亡后变孤儿钉子户
        return 0;
    }
    if (wcscmp(mode, L"ctrlbreak") == 0) {
        SetConsoleCtrlHandler(on_ctrl, TRUE);
        fprintf(stderr, "[child] ctrl handler installed, idling...\n");
        for (int i = 0; i < 300; i++)
            Sleep(200);
        return 0;
    }
    return 1;
}
