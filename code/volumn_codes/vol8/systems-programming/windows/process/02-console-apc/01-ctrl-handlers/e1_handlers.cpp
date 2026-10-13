// E1:SetConsoleCtrlHandler 全家福 —— 注册/注销、链序(后注册先调)、TRUE 拦截 vs FALSE 传递、
//     NULL+TRUE/FALSE 忽略开关,以及 WSL interop 启动链把 Ctrl+C 忽略位设上的现场揭秘。
// MSDN 口径(SetConsoleCtrlHandler Remarks,2026-10-04 取自 Microsoft Learn):
//   "called on a last-registered, first-called basis until one of the handlers returns TRUE.
//    If none of the handlers returns TRUE, the default handler is called."
//   默认 handler = ExitProcess。
//   NULL+TRUE:"causes the calling process to ignore CTRL+C input",且该属性被子进程继承。
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>

static const char* g_chain[8];
static LONG g_n = 0;
static volatile LONG g_intercept = 0; // handlerC 的拦截开关

static void note(const char* who) {
    g_chain[InterlockedIncrement(&g_n) - 1] = who;
}

static BOOL WINAPI handlerZ(DWORD) {
    note("Z(兜底TRUE)");
    return TRUE;
}
static BOOL WINAPI handlerA(DWORD) {
    note("A(FALSE传递)");
    return FALSE;
}
static BOOL WINAPI handlerB(DWORD) {
    note("B(FALSE传递)");
    return FALSE;
}
static BOOL WINAPI handlerC(DWORD) {
    LONG x = InterlockedCompareExchange(&g_intercept, 0, 0);
    note(x ? "C(TRUE拦截)" : "C(FALSE传递)");
    return x ? TRUE : FALSE;
}

static void send(DWORD ev) {
    GenerateConsoleCtrlEvent(ev, 0);
}

static void dump(const char* tag) {
    printf("    %s 调用链: ", tag);
    if (!g_n)
        printf("(空 —— 没有任何 handler 被调)");
    for (int i = 0; i < g_n; i++)
        printf("%s%s", i ? " -> " : "", g_chain[i]);
    printf("\n");
    g_n = 0; // 上一轮 handler 线程早已返回,无竞态
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[E1] pid=%lu main_tid=%lu\n", GetCurrentProcessId(), GetCurrentThreadId());

    SetConsoleCtrlHandler(handlerZ, TRUE); // 最先注册 = 链尾兜底
    SetConsoleCtrlHandler(handlerA, TRUE);
    SetConsoleCtrlHandler(handlerB, TRUE);
    SetConsoleCtrlHandler(handlerC, TRUE);

    printf("[1] 链 Z,A,B,C 全 FALSE(C 先 FALSE,Z 兜底 TRUE),发 CTRL_BREAK_EVENT\n");
    Sleep(80);
    send(CTRL_BREAK_EVENT);
    Sleep(400);
    dump("[1]");

    InterlockedIncrement(&g_intercept);
    printf("[2] C 改返回 TRUE:同一条链再发 CTRL_BREAK,期望链在 C 截断\n");
    Sleep(80);
    send(CTRL_BREAK_EVENT);
    Sleep(400);
    dump("[2]");

    BOOL ok = SetConsoleCtrlHandler(handlerC, FALSE); // 注销
    printf("[3] SetConsoleCtrlHandler(C,FALSE) 注销 C -> %d,再发,期望 B->A->Z\n", ok);
    Sleep(80);
    send(CTRL_BREAK_EVENT);
    Sleep(400);
    dump("[3]");

    printf("[4] 现在发 CTRL_C_EVENT —— 本进程由 WSL interop 链启动,忽略位被设上,期望空\n");
    Sleep(80);
    send(CTRL_C_EVENT);
    Sleep(400);
    dump("[4]");

    ok = SetConsoleCtrlHandler(NULL, FALSE); // 复位忽略位
    printf("[5] SetConsoleCtrlHandler(NULL,FALSE) 复位忽略位 -> %d,再发 CTRL_C,期望 B->A->Z\n", ok);
    Sleep(80);
    send(CTRL_C_EVENT);
    Sleep(400);
    dump("[5]");

    ok = SetConsoleCtrlHandler(NULL, TRUE); // 显式忽略
    printf("[6] SetConsoleCtrlHandler(NULL,TRUE) 显式忽略 Ctrl+C -> %d:发 CTRL_C 期望空;\n", ok);
    Sleep(80);
    send(CTRL_C_EVENT);
    Sleep(400);
    dump("[6-CTRL_C]");
    printf("    但紧接着发 CTRL_BREAK —— BREAK 不受忽略位限制,期望 B->A->Z\n");
    Sleep(80);
    send(CTRL_BREAK_EVENT);
    Sleep(400);
    dump("[6-CTRL_BREAK]");

    SetConsoleCtrlHandler(NULL, FALSE); // 收尾复位
    printf("[E1] done\n");
    return 0;
}
