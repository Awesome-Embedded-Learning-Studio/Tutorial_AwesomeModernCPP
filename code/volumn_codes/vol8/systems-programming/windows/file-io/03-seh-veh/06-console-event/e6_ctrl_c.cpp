// e6:Ctrl+C 是控制台事件,不是 SEH 异常
// 对照组:VEH 在岗 + SetConsoleCtrlHandler 在岗,然后自己给自己发 CTRL_C_EVENT。
// 若 Ctrl+C 走异常路径,VEH 应当看到分发。实测:ctrl handler 在【另一个线程】跑了
// (handler 日志里 thread id 与主线程不同),VEH 一个分发都没有 —— 两条铁轨。
// 细节:CTRL_BREAK_EVENT(=1)必达;CTRL_C_EVENT(=0)受控制台输入模式影响,
// 重定向/无交互的控制台里可能被吞(本机 run_e6.bat 的控制台就吞了 CTRL_C)。
// (handler 里用 CreateFile 直接写日志文件:ctrl 线程里 stdout 的缓冲不可靠)
#define WIN32_LEAN_AND_MEAN
#include <csignal>
#include <cstdio>
#include <windows.h>

static int veh_dispatches = 0;
static const char* LOG = "C:/msys64/tmp/e6_handler.log";

static void logf(const char* msg) {
    HANDLE f =
        CreateFileA(LOG, FILE_APPEND_DATA, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;
    DWORD w = 0;
    WriteFile(f, msg, (DWORD)lstrlenA(msg), &w, NULL);
    CloseHandle(f);
}

static LONG WINAPI veh(PEXCEPTION_POINTERS ep) {
    ++veh_dispatches;
    printf("  [VEH dispatch #%d] code=0x%08lX\n", veh_dispatches,
           (unsigned long)ep->ExceptionRecord->ExceptionCode);
    logf("VEH dispatch\n");
    return EXCEPTION_CONTINUE_SEARCH;
}

static BOOL WINAPI ctrl_handler(DWORD type) {
    char buf[128];
    wsprintfA(buf, "ctrl handler: type=%lu (thread=%lu)\n", type, GetCurrentThreadId());
    logf(buf);
    return TRUE; // 处理了,不要默认终止
}

static void sigint_handler(int) {
    logf("signal(SIGINT) handler ran\n");
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    DeleteFileA(LOG);
    AddVectoredExceptionHandler(1, veh);
    BOOL okh = SetConsoleCtrlHandler(ctrl_handler, TRUE);
    signal(SIGINT, sigint_handler);
    printf("SetConsoleCtrlHandler -> %d, console cp=%u\n", okh, GetConsoleCP());

    BOOL ok = GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
    printf("GenerateConsoleCtrlEvent(CTRL_C_EVENT) -> %d (CTRL_C_EVENT=0)\n", ok);
    Sleep(2000);
    ok = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0);
    printf("GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT) -> %d (CTRL_BREAK_EVENT=1)\n", ok);
    Sleep(2000);

    printf("veh_dispatches=%d(0 = Ctrl+C 从没进过 SEH 分发器)\n", veh_dispatches);
    printf("done\n");
    return 0;
}
