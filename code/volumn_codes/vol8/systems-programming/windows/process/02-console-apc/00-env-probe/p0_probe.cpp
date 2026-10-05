// P0:interop 控制台边界探针 —— 本机控制台到底是什么,CTRL_C/CTRL_BREAK 各到不到 handler
// 变量:直跑(WSL interop)/ cmd.exe /c / start 新窗口;ENABLE_PROCESSED_INPUT 开关
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>

static volatile LONG g_calls = 0;

static BOOL WINAPI handler(DWORD type) {
    InterlockedIncrement(&g_calls);
    printf("    [handler] type=%lu tid=%lu\n", type, GetCurrentThreadId());
    return TRUE; // 已处理,拦住默认终止
}

static const char* file_type_name(DWORD t) {
    switch (t) {
        case FILE_TYPE_CHAR:
            return "CHAR(console)";
        case FILE_TYPE_PIPE:
            return "PIPE";
        case FILE_TYPE_DISK:
            return "DISK";
        default:
            return "other";
    }
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("pid=%lu main_tid=%lu\n", GetCurrentProcessId(), GetCurrentThreadId());
    printf("GetConsoleWindow=%p  GetConsoleCP=%u\n", GetConsoleWindow(), GetConsoleCP());

    DWORD in_type = GetFileType(GetStdHandle(STD_INPUT_HANDLE));
    DWORD out_type = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
    printf("stdin FileType=%lu(%s)  stdout FileType=%lu(%s)\n", in_type, file_type_name(in_type),
           out_type, file_type_name(out_type));

    DWORD procs[8] = {};
    DWORD n = GetConsoleProcessList(procs, 8);
    printf("GetConsoleProcessList n=%lu:", n);
    for (DWORD i = 0; i < n && i < 8; i++)
        printf(" %lu", procs[i]);
    printf("\n");

    HANDLE conin = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD mode = 0;
    if (conin != INVALID_HANDLE_VALUE && GetConsoleMode(conin, &mode)) {
        printf("CONIN$ mode=0x%lX (ENABLE_PROCESSED_INPUT bit=%d)\n", mode,
               (int)!!(mode & ENABLE_PROCESSED_INPUT));
    } else {
        printf("CONIN$ open/mode failed gle=%lu\n", GetLastError());
    }

    SetConsoleCtrlHandler(handler, TRUE);

    LONG before = g_calls;
    printf("[send CTRL_C_EVENT(0)] -> %d\n", GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0));
    Sleep(800);
    printf("    calls after CTRL_C: %ld\n", g_calls - before);

    before = g_calls;
    printf("[send CTRL_BREAK_EVENT(1)] -> %d\n", GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0));
    Sleep(800);
    printf("    calls after CTRL_BREAK: %ld\n", g_calls - before);

    if (conin != INVALID_HANDLE_VALUE) {
        SetConsoleMode(conin, mode | ENABLE_PROCESSED_INPUT);
        before = g_calls;
        printf("[PROCESSED_INPUT forced ON, resend CTRL_C] -> %d\n",
               GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0));
        Sleep(800);
        printf("    calls after forced-CTRL_C: %ld\n", g_calls - before);
        CloseHandle(conin);
    }
    printf("total handler calls=%ld\n", g_calls);
    return 0;
}
