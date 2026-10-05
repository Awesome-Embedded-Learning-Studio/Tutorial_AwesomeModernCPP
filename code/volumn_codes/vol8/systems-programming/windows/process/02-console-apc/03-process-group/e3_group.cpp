// E3:进程组语义 —— CREATE_NEW_PROCESS_GROUP 的子进程对 Ctrl+C 免疫(实测);
//     免疫的实现 = 启动时隐式 SetConsoleCtrlHandler(NULL,TRUE)(CreateProcess 文档口径),
//     子进程自己 NULL+FALSE 可解除;GenerateConsoleCtrlEvent 定向发组;
//     CTRL_C 定向发组(非零 pgid)文档说"返回成功但收不到",实测验证。
// 用法:直接跑 = 父进程;`e3 child <A|B> <ready事件名> [reenable事件名]` = 子进程。
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>

static const char* g_tag = "?";
static HANDLE g_mu = NULL; // 跨进程打印锁:父/子共用一个命名互斥体,防三进程 printf 交错

static void plock() {
    if (!g_mu)
        g_mu = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, "Local\\e3_mu");
    if (g_mu)
        WaitForSingleObject(g_mu, INFINITE);
}
static void punlock() {
    if (g_mu)
        ReleaseMutex(g_mu);
}
#define PF(...)              \
    do {                     \
        plock();             \
        printf(__VA_ARGS__); \
        punlock();           \
    } while (0)

static BOOL WINAPI ch(DWORD type) {
    PF("    [%s handler] type=%lu tid=%lu\n", g_tag, type, GetCurrentThreadId());
    return TRUE; // 子进程都拦住,免疫演示不掺默认终止
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc >= 4 && lstrcmpiA(argv[1], "child") == 0) {
        BOOL isB = argv[2][0] == 'B';
        g_tag = isB ? "B" : "A";
        SetConsoleCtrlHandler(ch, TRUE);
        if (!isB) {
            SetConsoleCtrlHandler(NULL, FALSE); // A:同组,复位忽略位,正常收 Ctrl+C
        }
        // B:什么也不动 —— 保留 CREATE_NEW_PROCESS_GROUP 留下的隐式忽略位
        PF("  [child-%s] pid=%lu 就绪 %s\n", argv[2], GetCurrentProcessId(),
           isB ? "(新组 pgid=自己,Ctrl+C 忽略位=继承的 ON)" : "(同组,Ctrl+C 忽略位=复位)");
        HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, argv[3]);
        if (ready)
            SetEvent(ready);

        HANDLE exit_ev = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, "Local\\e3_exit");
        if (isB && argc >= 5) {
            HANDLE reen = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, argv[4]);
            HANDLE hs[2] = {reen, exit_ev};
            DWORD w = WaitForMultipleObjects(2, hs, FALSE, INFINITE);
            if (w == WAIT_OBJECT_0) {               // reenable 先到
                SetConsoleCtrlHandler(NULL, FALSE); // B 解除免疫
                PF("  [child-B] 已 SetConsoleCtrlHandler(NULL,FALSE):免疫解除\n");
                WaitForSingleObject(exit_ev, INFINITE); // 之后只等退出
            }
        } else {
            if (exit_ev)
                WaitForSingleObject(exit_ev, INFINITE);
        }
        PF("  [child-%s] 收到退出信号,正常返回 exit=0\n", argv[2]);
        return 0;
    }

    // ---------------- 父进程 ----------------
    PF("[E3] 父进程 pid=%lu\n", GetCurrentProcessId());
    g_tag = "P";
    SetConsoleCtrlHandler(ch, TRUE);
    SetConsoleCtrlHandler(NULL, FALSE);               // 父进程自己复位忽略位
    g_mu = CreateMutexA(NULL, FALSE, "Local\\e3_mu"); // 打印锁先建好,再放子进程

    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);
    HANDLE aReady = CreateEventA(NULL, FALSE, FALSE, "Local\\e3_a_ready");
    HANDLE bReady = CreateEventA(NULL, FALSE, FALSE, "Local\\e3_b_ready");
    HANDLE bReen = CreateEventA(NULL, FALSE, FALSE, "Local\\e3_b_reenable");
    HANDLE exitEv = CreateEventA(NULL, TRUE, FALSE, "Local\\e3_exit");

    char cmdA[800], cmdB[900];
    wsprintfA(cmdA, "\"%s\" child A Local\\e3_a_ready", self);
    wsprintfA(cmdB, "\"%s\" child B Local\\e3_b_ready Local\\e3_b_reenable", self);
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION piA = {}, piB = {};

    CreateProcessA(NULL, cmdA, NULL, NULL, TRUE, 0, NULL, NULL, &si, &piA);
    WaitForSingleObject(aReady, 5000);
    PF("[0] child-A 就绪(同组,共享控制台)\n");
    CreateProcessA(NULL, cmdB, NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &piB);
    WaitForSingleObject(bReady, 5000);
    PF("[0] child-B 就绪(CREATE_NEW_PROCESS_GROUP,pgid=B自己=%lu)\n", piB.dwProcessId);
    Sleep(300);

    PF("[1] 父发 CTRL_C(0):广播到共享本控制台者 —— 期望 P、A 响应,B 静默(免疫)\n");
    Sleep(80);
    GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
    Sleep(700);
    PF("    (上面若只有 P 和 A 两行 handler,type=0 —— B 一行都没有)\n");

    PF("[2] 父发 CTRL_BREAK(0):BREAK 不受忽略位限制 —— 期望 P、A、B 全响应\n");
    Sleep(80);
    GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0);
    Sleep(700);

    PF("[3] 通知 B 自己解除免疫(NULL+FALSE)\n");
    SetEvent(bReen);
    Sleep(500);

    PF("[4] 父发 CTRL_C(0, 定向 B 组 pgid=%lu):文档口径\"返回成功但收不到\" —— 实测:\n",
       piB.dwProcessId);
    BOOL ok = GenerateConsoleCtrlEvent(CTRL_C_EVENT, piB.dwProcessId);
    Sleep(80);
    PF("    返回值=%d,下面有没有人响应?\n", ok);
    Sleep(700);

    PF("[5] 父发 CTRL_BREAK(1, 定向 B 组 pgid=%lu):能定向 —— 期望只有 B 响应(P/A 不动)\n",
       piB.dwProcessId);
    ok = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, piB.dwProcessId);
    Sleep(80);
    PF("    返回值=%d\n", ok);
    Sleep(700);

    PF("[6] 父再发 CTRL_C(0) 广播:B 已解除免疫 —— 期望 P、A、B 全响应\n");
    Sleep(80);
    GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
    Sleep(700);

    SetEvent(exitEv);
    HANDLE both[2] = {piA.hProcess, piB.hProcess};
    WaitForMultipleObjects(2, both, TRUE, 5000);
    DWORD ca = 0, cb = 0;
    GetExitCodeProcess(piA.hProcess, &ca);
    GetExitCodeProcess(piB.hProcess, &cb);
    PF("[尾] A exit=%lu  B exit=%lu(都 0 = 免疫是「不投递」,不是「杀掉」)\n", ca, cb);
    PF("[E3] done\n");
    return 0;
}
