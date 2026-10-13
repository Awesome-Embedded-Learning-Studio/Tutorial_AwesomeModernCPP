// e5:混用与换行——经典 API 与 VT 写的是同一块缓冲,但规矩各自带着
//   1) 光标的归属:VT 序列定位+推进光标;WriteConsoleOutput 整块直写不碰光标;
//      两套坐标互不打架,但"下一次写在哪"只由光标说了算
//   2) ENABLE_WRAP_AT_EOL_OUTPUT:关掉之后,行尾那一格反复被覆盖,长串只剩最后一个字符
//   3) DISABLE_NEWLINE_AUTO_RETURN:行尾的 \n 是只换行还是连回车一起做,读回定位说话
#include <cstdio>
#include <windows.h>
static HANDLE co;
static void clear_rows(int y0, int n) {
    DWORD w = 0;
    for (int y = y0; y < y0 + n; ++y) {
        FillConsoleOutputCharacterA(co, ' ', 120, {0, (SHORT)y}, &w);
        FillConsoleOutputAttribute(co, 0x7, 120, {0, (SHORT)y}, &w);
    }
}
static COORD find_ch(char c, int y0, int rows) {
    CHAR_INFO back[130];
    for (int y = y0; y < y0 + rows; ++y) {
        COORD sz{120, 1}, org{0, 0};
        SMALL_RECT rc{0, (SHORT)y, 119, (SHORT)y};
        ReadConsoleOutput(co, back, sz, org, &rc);
        for (int x = 0; x < 120; ++x)
            if ((char)back[x].Char.UnicodeChar == c)
                return {(SHORT)x, (SHORT)y};
    }
    return {-1, -1};
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    co = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                     nullptr, OPEN_EXISTING, 0, nullptr);
    DWORD m0 = 0;
    GetConsoleMode(co, &m0);
    DWORD wr = 0;
    SetConsoleMode(co, m0 | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    // [1] 光标归属
    clear_rows(0, 4);
    SetConsoleCursorPosition(co, {10, 0});
    CHAR_INFO cell{};
    cell.Char.UnicodeChar = L'#';
    cell.Attributes = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    CHAR_INFO grid[4];
    for (int i = 0; i < 4; ++i)
        grid[i] = cell;
    COORD sz{4, 1}, org{0, 0};
    SMALL_RECT dst{40, 2, 43, 2};
    WriteConsoleOutput(co, grid, sz, org, &dst);     // 直写到 (40,2),不碰光标
    WriteConsoleA(co, "\x1b[2;5H", 6, &wr, nullptr); // VT 定位到 (4,1)
    WriteConsoleA(co, "A", 1, &wr, nullptr);         // 写在光标处,光标推进
    WriteConsoleA(co, "B", 1, &wr, nullptr);
    CONSOLE_SCREEN_BUFFER_INFO bi{};
    GetConsoleScreenBufferInfo(co, &bi);
    COORD pa = find_ch('A', 0, 4), pb = find_ch('B', 0, 4), ph = find_ch('#', 0, 4);
    printf("[1] A 落在 (%d,%d),B 落在 (%d,%d)(跟光标走);'#' 落在 (%d,%d)(WriteConsoleOutput "
           "自带坐标)\n",
           pa.X, pa.Y, pb.X, pb.Y, ph.X, ph.Y);
    printf("    此刻光标=(%d,%d) —— 两套写法互不干扰,但光标只认 VT/WriteConsole 一系\n",
           bi.dwCursorPosition.X, bi.dwCursorPosition.Y);

    // [2] 行尾回绕开关
    for (int on = 1; on >= 0; --on) {
        DWORD base = m0 | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(co, on ? (base | ENABLE_WRAP_AT_EOL_OUTPUT)
                              : (base & ~(DWORD)ENABLE_WRAP_AT_EOL_OUTPUT));
        clear_rows(6, 2);
        SetConsoleCursorPosition(co, {115, 6}); // 行宽 120:115 起写 8 字符,必然越过行尾
        WriteConsoleA(co, "ABCDEFGH", 8, &wr, nullptr);
        COORD pv = find_ch('A', 6, 2), pz = find_ch('H', 6, 2);
        printf("[2] WRAP_AT_EOL %s:光标起 (115,6) 写 \"ABCDEFGH\",A=(%d,%d) H=(%d,%d)",
               on ? "开" : "关", pv.X, pv.Y, pz.X, pz.Y);
        if (on)
            printf(" —— 越过行尾回绕到下一行,E..H 在第二行\n");
        else
            printf(" —— 越过行尾不回绕,E..H 反复覆盖 (119,6),只剩 H\n");
    }
    SetConsoleMode(co, m0 | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_WRAP_AT_EOL_OUTPUT);

    // [3] 行尾 \n 的两种解释
    for (int dis = 0; dis <= 1; ++dis) {
        SetConsoleMode(co, (m0 | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_WRAP_AT_EOL_OUTPUT) |
                               (dis ? DISABLE_NEWLINE_AUTO_RETURN : 0));
        clear_rows(9, 3);
        SetConsoleCursorPosition(co, {117, 9});
        WriteConsoleA(co, "AB\nC", 4, &wr, nullptr);
        COORD pb2 = find_ch('B', 9, 3), pc = find_ch('C', 9, 3);
        printf("[3] DISABLE_NEWLINE_AUTO_RETURN %s:B=(%d,%d) C=(%d,%d)%s\n", dis ? "设" : "不设",
               pb2.X, pb2.Y, pc.X, pc.Y,
               dis ? " —— \\n 只下移一行不回零,C 落在 (119,10)"
                   : " —— 行尾 \\n 连回车一起做,C 到下一行行首");
    }

    clear_rows(0, 12);
    SetConsoleMode(co, m0);
    return 0;
}
