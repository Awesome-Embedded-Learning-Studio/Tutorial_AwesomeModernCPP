// e2:字符缓冲区——控制台的屏幕是 CHAR_INFO 的网格,WriteConsoleOutput 整块直写
//   1) 缓冲区与窗口是两个概念:dwSize(120x9001,带滚动历史) vs srWindow(120x30,可见窗)
//   2) 6x10 的彩色网格直写到指定坐标,ReadConsoleOutput 原样读回对表(颜色落在属性字节上)
//   3) WriteConsoleOutput 不动光标;WriteConsoleA 在光标处写并推进光标——两种写法的坐标系
//   4) 越界裁剪:矩形超出缓冲区右缘,超出部分丢弃,能放下的照写
//   5) ScrollConsoleScreenBuffer:把一块区域搬走,空出的地方填指定字符与属性
#include <cstdio>
#include <windows.h>
static HANDLE co;
static const char* attr_desc(WORD a) {
    static char b[128];
    snprintf(b, sizeof b, "%s%s%s|%s%s%s", (a & FOREGROUND_RED) ? "R" : "-",
             (a & FOREGROUND_GREEN) ? "G" : "-", (a & FOREGROUND_BLUE) ? "B" : "-",
             (a & BACKGROUND_RED) ? "R" : "-", (a & BACKGROUND_GREEN) ? "G" : "-",
             (a & BACKGROUND_BLUE) ? "B" : "-");
    return b;
}
int main() {
    co = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                     nullptr, OPEN_EXISTING, 0, nullptr);
    CONSOLE_SCREEN_BUFFER_INFO bi{};
    GetConsoleScreenBufferInfo(co, &bi);
    printf("[1] 缓冲区 dwSize=%dx%d(带滚动历史) 可见窗 srWindow=%dx%d 光标=(%d,%d) 默认属性=0x%x\n",
           bi.dwSize.X, bi.dwSize.Y, bi.srWindow.Right - bi.srWindow.Left + 1,
           bi.srWindow.Bottom - bi.srWindow.Top + 1, bi.dwCursorPosition.X, bi.dwCursorPosition.Y,
           bi.wAttributes);

    // [2] 6 行 x 10 列彩色矩阵
    const int W = 10, H = 6;
    CHAR_INFO grid[H * W];
    for (int r = 0; r < H; ++r)
        for (int c = 0; c < W; ++c) {
            grid[r * W + c].Char.UnicodeChar = (WCHAR)('A' + r);
            grid[r * W + c].Attributes =
                (WORD)((r < 3 ? FOREGROUND_INTENSITY : 0) | (r % 2 ? BACKGROUND_BLUE : 0) |
                       FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        }
    COORD size{W, H}, origin{0, 0};
    SMALL_RECT dst{2, 0, 2 + W - 1, H - 1}; // 写到 (2,0) 起
    COORD before = bi.dwCursorPosition;
    BOOL ok = WriteConsoleOutput(co, grid, size, origin, &dst);
    GetConsoleScreenBufferInfo(co, &bi);
    printf("[2] WriteConsoleOutput(6x10 @ (2,0)) -> %d;光标 (%d,%d)->(%d,%d):%s\n", ok, before.X,
           before.Y, bi.dwCursorPosition.X, bi.dwCursorPosition.Y,
           (before.X == bi.dwCursorPosition.X && before.Y == bi.dwCursorPosition.Y)
               ? "整块直写不碰光标"
               : "光标动了?!");

    // 读回对表
    CHAR_INFO back[H * W];
    SMALL_RECT src{2, 0, 2 + W - 1, H - 1};
    ReadConsoleOutput(co, back, size, origin, &src);
    int same = 0;
    for (int i = 0; i < H * W; ++i)
        if (back[i].Char.UnicodeChar == grid[i].Char.UnicodeChar &&
            back[i].Attributes == grid[i].Attributes)
            ++same;
    printf("    ReadConsoleOutput 读回 60 格,字符+属性全对:%d/60\n", same);
    printf("    第 0 行属性=%s(亮白字)\n", attr_desc(back[0].Attributes));
    printf("    第 1 行属性=%s(亮字/蓝底)\n", attr_desc(back[W].Attributes));

    // [3] 越界裁剪:x=115 起 10 列宽,缓冲区只有 120 列
    SMALL_RECT clip{115, 0, 115 + W - 1, H - 1};
    WriteConsoleOutput(co, grid, size, origin, &clip);
    CHAR_INFO back2[5 * H];
    COORD s5{5, H};
    SMALL_RECT src2{115, 0, 119, H - 1};
    ReadConsoleOutput(co, back2, s5, origin, &src2);
    printf("[3] 写 10 列宽 @x=115,实际落下的矩形 Right=%d(裁到缓冲区右缘 119,只写进 5 列),"
           "读回首格字符='%c'\n",
           clip.Right, (char)back2[0].Char.UnicodeChar);

    // [4] WriteConsoleA:光标处写、写完推进
    SetConsoleCursorPosition(co, {40, 8});
    DWORD written = 0;
    WriteConsoleA(co, "hi", 2, &written, nullptr);
    GetConsoleScreenBufferInfo(co, &bi);
    printf(
        "[4] SetConsoleCursorPosition(40,8)+WriteConsoleA(\"hi\"):写 %lu 字节,光标推进到 (%d,%d)\n",
        (unsigned long)written, bi.dwCursorPosition.X, bi.dwCursorPosition.Y);

    // [5] ScrollConsoleScreenBuffer:把第 0-5 行上滚 2 行,空出的两行填 '.'
    SMALL_RECT scroll{0, 0, 119, 5};
    SMALL_RECT clipall{0, 0, 119, 5};
    CHAR_INFO fill{};
    fill.Char.UnicodeChar = L'.';
    fill.Attributes = FOREGROUND_RED;
    ScrollConsoleScreenBuffer(co, &scroll, &clipall, {0, -2}, &fill);
    CHAR_INFO top[4];
    COORD s4{4, 1};
    SMALL_RECT sr{2, 0, 5, 0};
    ReadConsoleOutput(co, top, s4, {0, 0}, &sr);
    printf(
        "[5] 上滚 2 行后第 0 行读回:\"%c%c%c%c\"(原第 2 行顶上来;空出的底部两行由红字 '.' 填补)\n",
        (char)top[0].Char.UnicodeChar, (char)top[1].Char.UnicodeChar, (char)top[2].Char.UnicodeChar,
        (char)top[3].Char.UnicodeChar);
    // 收尾:把玩过的区域清干净
    DWORD cw = 0;
    COORD home{0, 0};
    FillConsoleOutputCharacterA(co, ' ', 120 * 10, home, &cw);
    FillConsoleOutputAttribute(co, bi.wAttributes, 120 * 10, home, &cw);
    SetConsoleCursorPosition(co, home);
    return 0;
}
