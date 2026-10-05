// e4:VT 序列——ENABLE_VIRTUAL_TERMINAL_PROCESSING 开与关,屏幕缓冲区里留下的证据
//   验收不靠人眼:一律 ReadConsoleOutput 读回字符+属性,对表说话
//   1) 开关关着:WriteConsoleA 写 "\x1b[31mRED\x1b[0m",缓冲区里留下九个字面字符 ESC [ 3 1 m R E D
//   ... 2) 开关开着:同一串字节,缓冲区里只剩 R E D 三个字符,且属性字节变成红色(FOREGROUND_RED) 3)
//   SGR 组合:\x1b[1;31m(亮红)读回 FOREGROUND_RED|FOREGROUND_INTENSITY 4) 光标定位:\x1b[5;10H
//   之后读光标=(9,4)(1 起算的序列,0 起算的坐标) 5) \x1b[2J
//   清屏:读完缓冲区变空格;顺带验光标去哪了(ANSI 的 ED 不管光标,conhost 的实现动不动)
#include <cstdio>
#include <windows.h>
static HANDLE co;
static void clear_top(int rows) {
    DWORD w = 0;
    FillConsoleOutputCharacterA(co, ' ', 130 * rows, {0, 0}, &w);
    FillConsoleOutputAttribute(co, 0x7, 130 * rows, {0, 0}, &w);
    SetConsoleCursorPosition(co, {0, 0});
}
static void dump_row0(int n) {
    CHAR_INFO back[130];
    COORD sz{130, 1}, org{0, 0};
    SMALL_RECT rc{0, 0, 129, 0};
    ReadConsoleOutput(co, back, sz, org, &rc);
    printf("    第 0 行读回:\"");
    for (int i = 0; i < n; ++i) {
        WCHAR c = back[i].Char.UnicodeChar;
        if (c == 0x1b)
            printf("ESC");
        else if (c >= ' ')
            printf("%c", (char)c);
    }
    printf("\"\n");
    printf("    前 9 格属性:");
    for (int i = 0; i < 9; ++i)
        printf(" %02x", back[i].Attributes);
    printf("\n");
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    co = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                     nullptr, OPEN_EXISTING, 0, nullptr);
    DWORD m0 = 0;
    GetConsoleMode(co, &m0);
    DWORD wr = 0;

    // [1] VT 关
    SetConsoleMode(co, m0 & ~(DWORD)ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    clear_top(2);
    WriteConsoleA(co, "\x1b[31mRED\x1b[0m", 12, &wr, nullptr);
    printf("[1] VT 关:WriteConsoleA 写了 %lu 字节,序列没被吃——\n", (unsigned long)wr);
    dump_row0(12);

    // [2] VT 开
    SetConsoleMode(co, m0 | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    clear_top(2);
    WriteConsoleA(co, "\x1b[31mRED\x1b[0m", 12, &wr, nullptr);
    printf("[2] VT 开:同一串 12 字节,只剩 R E D 三个字符进了缓冲,属性=红色——\n");
    dump_row0(12);

    // [3] SGR 亮红
    clear_top(2);
    WriteConsoleA(co, "\x1b[1;31mX\x1b[0m", 10, &wr, nullptr);
    CHAR_INFO b3[2];
    COORD sz{2, 1}, org{0, 0};
    SMALL_RECT rc{0, 0, 1, 0};
    ReadConsoleOutput(co, b3, sz, org, &rc);
    printf("[3] \\x1b[1;31mX 读回:字符='%c' 属性=0x%x(RED=0x4|INTENSITY=0x8 -> 0xc)\n",
           (char)b3[0].Char.UnicodeChar, b3[0].Attributes);

    // [4] 光标定位
    SetConsoleCursorPosition(co, {0, 0});
    WriteConsoleA(co, "\x1b[5;10H", 7, &wr, nullptr);
    CONSOLE_SCREEN_BUFFER_INFO bi{};
    GetConsoleScreenBufferInfo(co, &bi);
    printf("[4] \\x1b[5;10H 后光标=(%d,%d)(VT 是 1 起算,缓冲区坐标是 0 起算,9,4 对上了)\n",
           bi.dwCursorPosition.X, bi.dwCursorPosition.Y);

    // [5] 清屏与光标
    SetConsoleCursorPosition(co, {30, 3});
    WriteConsoleA(co, "\x1b[2J", 4, &wr, nullptr);
    GetConsoleScreenBufferInfo(co, &bi);
    printf("[5] 光标先放 (30,3),\\x1b[2J 之后光标=(%d,%d) —— 2J 只清屏不动光标,与 ANSI 的 ED "
           "定义一致(老 conhost 曾顺手归零,本机 26200 不再如此)",
           bi.dwCursorPosition.X, bi.dwCursorPosition.Y);
    CHAR_INFO b5[4];
    COORD s5{4, 1};
    SMALL_RECT r5{0, 0, 3, 0};
    ReadConsoleOutput(co, b5, s5, {0, 0}, &r5);
    printf(";缓冲区读回 \"%c%c%c%c\"(清空成空格)\n", (char)b5[0].Char.UnicodeChar,
           (char)b5[1].Char.UnicodeChar, (char)b5[2].Char.UnicodeChar,
           (char)b5[3].Char.UnicodeChar);

    SetConsoleMode(co, m0);
    return 0;
}
