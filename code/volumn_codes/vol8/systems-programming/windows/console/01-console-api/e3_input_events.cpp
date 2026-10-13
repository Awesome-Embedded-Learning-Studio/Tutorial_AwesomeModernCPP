// e3:输入是一队事件记录——ReadConsoleInput 的世界观
//   1) 键盘的一按一松是两条
//   KEY_EVENT_RECORD,字段全录:KeyDown/Repeat/KeyCode/ScanCode/字符/修饰键状态 2)
//   队列可数(GetNumberOfConsoleInputEvents)、可窥(PeekConsoleInput 不取走)、可冲(Flush) 3)
//   鼠标、焦点也是记录:WriteConsoleInput 注入 MOUSE_EVENT / FOCUS_EVENT 原样读回 4)
//   WINDOW_BUFFER_SIZE_EVENT 由系统投:模式位 ENABLE_WINDOW_INPUT 是闸门,开着才进队列;
//      本机的无窗口控制台改不了缓冲区尺寸,系统侧路径走不通,记录形态用注入演示
//   5) 注入的 Ctrl+C(LEFT_CTRL_PRESSED + 'C')只是一条键记录,不会被转成控制事件
//      (process/02 的 README 已立此案,这里只演示记录形态,不再验信号路)
//   6) [5] 把两句探索期手测升级成读数:缓冲区尺寸原样重设的返回值与 gle;
//      SetConsoleWindowInfo 同尺寸/异尺寸两场,窗口改没改拿 GetConsoleScreenBufferInfo 读回 srWindow
//      对表,事件投没投拿队列计数说话,不写死任何文案
// 本机没有真人敲键,输入一律用 WriteConsoleInput 注入——这是记录的搬运,不是键的伪造
#include <cstdio>
#include <cstdlib>
#include <windows.h>
static HANDLE ci;
static const char* ctl_state(DWORD s) {
    static char b[160];
    b[0] = 0;
    if (s & LEFT_CTRL_PRESSED)
        strcat(b, "L-Ctrl ");
    if (s & RIGHT_ALT_PRESSED)
        strcat(b, "R-Alt ");
    if (s & SHIFT_PRESSED)
        strcat(b, "Shift ");
    if (s & CAPSLOCK_ON)
        strcat(b, "CapsLock ");
    return b[0] ? b : "(无)";
}
static void put_key(BOOL down, WORD vk, WORD scan, WCHAR ch, DWORD state, WORD repeat = 1) {
    INPUT_RECORD r{};
    r.EventType = KEY_EVENT;
    r.Event.KeyEvent = {down, repeat, vk, scan, ch, state};
    DWORD n = 0;
    WriteConsoleInput(ci, &r, 1, &n);
}
static void show(const INPUT_RECORD& r) {
    if (r.EventType == KEY_EVENT) {
        const KEY_EVENT_RECORD& k = r.Event.KeyEvent;
        printf("    KEY_EVENT  %s vk=0x%02x(%c) scan=0x%02x char=%c repeat=%u 修饰:%s\n",
               k.bKeyDown ? "按下" : "松开", k.wVirtualKeyCode,
               (k.wVirtualKeyCode >= ' ' && k.wVirtualKeyCode < 0x7f) ? (char)k.wVirtualKeyCode
                                                                      : '?',
               k.wVirtualScanCode, (char)k.uChar.UnicodeChar, k.wRepeatCount,
               ctl_state(k.dwControlKeyState));
    } else if (r.EventType == MOUSE_EVENT) {
        const MOUSE_EVENT_RECORD& m = r.Event.MouseEvent;
        printf("    MOUSE_EVENT 位置=(%d,%d) 按钮=0x%x 事件=0x%x 修饰:%s\n", m.dwMousePosition.X,
               m.dwMousePosition.Y, (unsigned)m.dwButtonState, (unsigned)m.dwEventFlags,
               ctl_state(m.dwControlKeyState));
    } else if (r.EventType == FOCUS_EVENT) {
        printf("    FOCUS_EVENT  bSetFocus=%d\n", r.Event.FocusEvent.bSetFocus);
    } else if (r.EventType == WINDOW_BUFFER_SIZE_EVENT) {
        printf("    WINDOW_BUFFER_SIZE_EVENT  新尺寸=%dx%d\n",
               r.Event.WindowBufferSizeEvent.dwSize.X, r.Event.WindowBufferSizeEvent.dwSize.Y);
    } else {
        printf("    EventType=%d\n", (int)r.EventType);
    }
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    ci = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                     nullptr, OPEN_EXISTING, 0, nullptr);
    DWORD n = 0;

    // [1] 注入 'A' 一按一松 + Ctrl+C 组合 + 鼠标 + 焦点
    FlushConsoleInputBuffer(ci);
    put_key(TRUE, 'A', 0x1e, L'A', 0);
    put_key(FALSE, 'A', 0x1e, L'A', 0);
    put_key(TRUE, 'C', 0x2e, L'c', LEFT_CTRL_PRESSED);
    INPUT_RECORD mr{};
    mr.EventType = MOUSE_EVENT;
    mr.Event.MouseEvent.dwMousePosition = {5, 3};
    mr.Event.MouseEvent.dwButtonState = FROM_LEFT_1ST_BUTTON_PRESSED;
    WriteConsoleInput(ci, &mr, 1, &n);
    INPUT_RECORD fr{};
    fr.EventType = FOCUS_EVENT;
    fr.Event.FocusEvent.bSetFocus = TRUE;
    WriteConsoleInput(ci, &fr, 1, &n);
    GetNumberOfConsoleInputEvents(ci, &n);
    printf("[1] 注入 5 条后,GetNumberOfConsoleInputEvents=%lu\n", (unsigned long)n);

    // [2] Peek 窥一眼不取走
    INPUT_RECORD peek[1];
    PeekConsoleInput(ci, peek, 1, &n);
    printf("[2] PeekConsoleInput 取 1 条(不取走),EventType=KEY_EVENT:%d,队列余量仍 ",
           peek[0].EventType == KEY_EVENT);
    GetNumberOfConsoleInputEvents(ci, &n);
    printf("%lu\n", (unsigned long)n);

    // [3] ReadConsoleInput 逐条取走
    printf("[3] ReadConsoleInput 逐条读:\n");
    for (int i = 0; i < 5; ++i) {
        ReadConsoleInput(ci, peek, 1, &n);
        show(peek[0]);
    }
    GetNumberOfConsoleInputEvents(ci, &n);
    printf("    取完后队列余量=%lu\n", (unsigned long)n);

    // [4] WINDOW_BUFFER_SIZE_EVENT:本机口径下系统侧触发不了,如实记录
    HANDLE co = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    CONSOLE_SCREEN_BUFFER_INFO bi{};
    GetConsoleScreenBufferInfo(co, &bi);
    DWORD mode0 = 0;
    GetConsoleMode(ci, &mode0);
    SetConsoleMode(ci, mode0 | ENABLE_WINDOW_INPUT);
    FlushConsoleInputBuffer(ci);
    SetLastError(0);
    BOOL okb = SetConsoleScreenBufferSize(co, {120, 3000});
    printf("[4] 开 ENABLE_WINDOW_INPUT 后 SetConsoleScreenBufferSize(120x3000) -> %d "
           "GetLastError=%lu\n",
           okb, GetLastError());
    printf("    于是这条事件的形态用注入演示:\n");
    INPUT_RECORD wr{};
    wr.EventType = WINDOW_BUFFER_SIZE_EVENT;
    wr.Event.WindowBufferSizeEvent.dwSize = {100, 30};
    WriteConsoleInput(ci, &wr, 1, &n);
    INPUT_RECORD r{};
    ReadConsoleInput(ci, &r, 1, &n);
    show(r);

    // [5] 探索期两句手测的正名:一切以读数为准
    FlushConsoleInputBuffer(ci); // 注入演示的记录清掉,队列从零数起
    SetLastError(0);
    BOOL ok_same = SetConsoleScreenBufferSize(co, bi.dwSize); // 原样重设(120x9001)
    printf("[5] SetConsoleScreenBufferSize(原样 %dx%d) -> %d GetLastError=%lu\n", bi.dwSize.X,
           bi.dwSize.Y, ok_same, GetLastError());

    SetLastError(0);
    SMALL_RECT w0 = bi.srWindow;
    BOOL ok_w_same = SetConsoleWindowInfo(co, TRUE, &w0); // 同尺寸:当前窗口原样喂回
    GetNumberOfConsoleInputEvents(ci, &n);
    printf("    SetConsoleWindowInfo(同尺寸 %dx%d 原样重设) -> %d GetLastError=%lu 队列=%lu\n",
           w0.Right - w0.Left + 1, w0.Bottom - w0.Top + 1, ok_w_same, GetLastError(),
           (unsigned long)n);

    SetLastError(0);
    SMALL_RECT w1 = w0; // 异尺寸:底行上收一行,缩成 29 行
    w1.Bottom = (SHORT)(w0.Bottom - 1);
    BOOL ok_w_diff = SetConsoleWindowInfo(co, TRUE, &w1);
    CONSOLE_SCREEN_BUFFER_INFO bi2{};
    GetConsoleScreenBufferInfo(co, &bi2);
    GetNumberOfConsoleInputEvents(ci, &n);
    printf("    SetConsoleWindowInfo(异尺寸 %dx%d) -> %d GetLastError=%lu 队列=%lu\n",
           w1.Right - w1.Left + 1, w1.Bottom - w1.Top + 1, ok_w_diff, GetLastError(),
           (unsigned long)n);
    printf("    读回 srWindow=%dx%d(窗口真改没改,以读回为准)\n",
           bi2.srWindow.Right - bi2.srWindow.Left + 1, bi2.srWindow.Bottom - bi2.srWindow.Top + 1);
    SetConsoleWindowInfo(co, TRUE, &w0); // 把窗口还回去,共享控制台不欠一行
    SetConsoleMode(ci, mode0);
    FlushConsoleInputBuffer(ci);
    return 0;
}
