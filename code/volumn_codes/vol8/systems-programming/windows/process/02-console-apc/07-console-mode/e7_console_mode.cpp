// E7(速览):GetConsoleMode/ENABLE_PROCESSED_INPUT —— Ctrl+C 处理开关。
//     开:Ctrl+C 走信号(handler 被调,输入缓冲不出现它);
//     关:Ctrl+C 降级为普通输入字节 0x03(ReadFile 能读到)。
//     详细留 ch06 终端篇。这里用 WriteConsoleInput 往输入缓冲注入一对 Ctrl+C 按键记录
//     (免键盘:agent 环境没有真人在控制台前敲 Ctrl+C,注入语义等价)。
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>

static volatile LONG g_calls = 0;

static BOOL WINAPI handler(DWORD) {
    InterlockedIncrement(&g_calls);
    return TRUE;
}

static void inject_ctrl_c(HANDLE conin) {
    INPUT_RECORD rec[2] = {};
    rec[0].EventType = KEY_EVENT;
    rec[0].Event.KeyEvent.bKeyDown = TRUE;
    rec[0].Event.KeyEvent.wRepeatCount = 1;
    rec[0].Event.KeyEvent.wVirtualKeyCode = 'C';
    rec[0].Event.KeyEvent.wVirtualScanCode = (WORD)MapVirtualKeyW('C', MAPVK_VK_TO_VSC);
    rec[0].Event.KeyEvent.uChar.UnicodeChar = 0x03; // Ctrl+C 的字符码
    rec[0].Event.KeyEvent.dwControlKeyState = LEFT_CTRL_PRESSED;
    rec[1] = rec[0];
    rec[1].Event.KeyEvent.bKeyDown = FALSE;
    DWORD n = 0;
    BOOL ok = WriteConsoleInputW(conin, rec, 2, &n);
    printf("    注入 Ctrl+C 键事件(按下+抬起,共 %lu 条)-> %d\n", n, ok);
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[E7] pid=%lu GetConsoleWindow=%p\n", GetCurrentProcessId(), GetConsoleWindow());
    SetConsoleCtrlHandler(handler, TRUE);
    SetConsoleCtrlHandler(NULL, FALSE); // 复位忽略位,让 Ctrl+C 走信号路径

    HANDLE conin = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD m0 = 0;
    GetConsoleMode(conin, &m0);
    printf("[0] CONIN$ 初始 mode=0x%lX:ENABLE_PROCESSED_INPUT=%d(1=Ctrl+C 走信号)\n", m0,
           (int)!!(m0 & ENABLE_PROCESSED_INPUT));

    printf("[1] 场景 A:PROCESSED_INPUT 开(默认),注入 Ctrl+C —— 期望 handler 被调,缓冲无残留\n");
    FlushConsoleInputBuffer(conin);
    g_calls = 0;
    inject_ctrl_c(conin);
    Sleep(300);
    DWORD cnt = 0;
    GetNumberOfConsoleInputEvents(conin, &cnt);
    printf("    handler 调用数=%ld,输入缓冲残留=%lu 条(0=被当信号消费)\n", g_calls, cnt);

    printf("[2] 场景 B:关掉 ENABLE_PROCESSED_INPUT(顺手关 LINE/ECHO),再注入 —— 期望读到 0x03\n");
    SetConsoleMode(conin,
                   m0 & ~(DWORD)(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
    FlushConsoleInputBuffer(conin);
    g_calls = 0;
    inject_ctrl_c(conin);
    DWORD w = WaitForSingleObject(conin, 1500); // 控制台输入句柄本身可等(WaitFor* 家族一员)
    printf("    WaitForSingleObject(CONIN$,1500)=%lu(0=缓冲有货)\n", w);
    unsigned char buf[8] = {};
    DWORD rd = 0;
    ReadFile(conin, buf, sizeof buf, &rd, NULL);
    printf("    ReadFile 读到 %lu 字节:", rd);
    for (DWORD i = 0; i < rd; i++)
        printf(" %02X", buf[i]);
    printf(" —— handler 调用数=%ld(0=没走信号,Ctrl+C 只是字节 0x03)\n", g_calls);

    SetConsoleMode(conin, m0); // 复原
    CloseHandle(conin);
    printf("[E7] done\n");
    return 0;
}
