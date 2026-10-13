// e1:控制台句柄与模式位——CONIN$/CONOUT$ 与 stdio 的解耦,输入输出两套模式的逐位清单
//   1) interop 启动链下 stdio 是管道:对它们调 GetConsoleMode 直接失败(GetLastError=6);
//      但进程确实挂着一个(无窗口的)控制台,CreateFile("CONIN$"/"CONOUT$") 直开就能用
//   2) 输入模式 9 个已文档位 + 输出模式 5 个位,逐位翻译,并实测 SetConsoleMode 改了能读回
//   3) ENABLE_VIRTUAL_TERMINAL_PROCESSING 默认关着(与文档口径一致)
// 分工:process/02 的 E7 已演过「关 ENABLE_PROCESSED_INPUT 后 Ctrl+C 降级为字节 0x03」,
//      本篇管全部模式位的地图与 VT 开关,不再重复那一幕。
#include <cstdio>
#include <windows.h>
struct Bit {
    DWORD bit;
    const char* name;
    const char* desc;
};
static const Bit in_bits[] = {
    {ENABLE_PROCESSED_INPUT, "PROCESSED_INPUT", "Ctrl+C 等转成控制事件(信号路)"},
    {ENABLE_LINE_INPUT, "LINE_INPUT", "ReadFile 行缓冲,回车才交货"},
    {ENABLE_ECHO_INPUT, "ECHO_INPUT", "输入回显"},
    {ENABLE_WINDOW_INPUT, "WINDOW_INPUT", "窗口尺寸变化投 WINDOW_BUFFER_SIZE_EVENT"},
    {ENABLE_MOUSE_INPUT, "MOUSE_INPUT", "投递鼠标事件记录"},
    {ENABLE_INSERT_MODE, "INSERT_MODE", "插入/覆盖(EXTENDED 的子开关)"},
    {ENABLE_QUICK_EDIT_MODE, "QUICK_EDIT_MODE", "鼠标选择与右键粘贴(EXTENDED 的子开关)"},
    {ENABLE_EXTENDED_FLAGS, "EXTENDED_FLAGS", "INSERT/QUICK_EDIT 的总开关"},
    {ENABLE_AUTO_POSITION, "AUTO_POSITION", "窗口出现的位置由系统决定"},
    {ENABLE_VIRTUAL_TERMINAL_INPUT, "VIRTUAL_TERMINAL_INPUT", "输入改走 VT 序列字节,不合成键事件"},
};
static const Bit out_bits[] = {
    {ENABLE_PROCESSED_OUTPUT, "PROCESSED_OUTPUT", "处理退格/制表/响铃等控制字符"},
    {ENABLE_WRAP_AT_EOL_OUTPUT, "WRAP_AT_EOL_OUTPUT", "行尾自动换行"},
    {ENABLE_VIRTUAL_TERMINAL_PROCESSING, "VIRTUAL_TERMINAL_PROCESSING", "解析输出的 VT 转义序列"},
    {DISABLE_NEWLINE_AUTO_RETURN, "DISABLE_NEWLINE_AUTO_RETURN", "行尾换行不自动带回车"},
    {ENABLE_LVB_GRID_WORLDWIDE, "LVB_GRID_WORLDWIDE", "网格字形属性(罕见字体)"},
};
static void dump(const char* tag, DWORD v, const Bit* tab, int n) {
    printf("%s = 0x%04lx:", tag, (unsigned long)v);
    for (int i = 0; i < n; ++i)
        if (v & tab[i].bit)
            printf(" %s", tab[i].name);
    DWORD known = 0;
    for (int i = 0; i < n; ++i)
        known |= tab[i].bit;
    if (v & ~known)
        printf(" (+未文档位 0x%lx)", (unsigned long)(v & ~known));
    printf("\n");
    for (int i = 0; i < n; ++i)
        if (v & tab[i].bit)
            printf("    %-26s %s\n", tab[i].name, tab[i].desc);
}
int main() {
    // 1) stdio 是管道
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE), hout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dummy;
    BOOL a = GetConsoleMode(hin, &dummy), b = GetConsoleMode(hout, &dummy);
    printf("[1] GetConsoleMode(stdin)=%d GetConsoleMode(stdout)=%d GetLastError(后一次)=%ld\n", a,
           b, GetLastError());
    printf("    stdio 挂的是管道;控制台要用 CONIN$/CONOUT$ 直开\n");

    HANDLE cin =
        CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE co = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

    DWORD mi = 0, mo = 0;
    GetConsoleMode(cin, &mi);
    GetConsoleMode(co, &mo);
    printf("[2] 出厂态:\n");
    dump("    CONIN$ mode", mi, in_bits, 9);
    dump("    CONOUT$ mode", mo, out_bits, 5);

    // 3) 改了能读回:全关再全开
    DWORD keep_i = mi;
    SetConsoleMode(cin, 0);
    GetConsoleMode(cin, &mi);
    printf("[3] SetConsoleMode(cin, 0) 后读回:0x%04lx(全关生效)\n", (unsigned long)mi);
    SetConsoleMode(cin, keep_i);
    DWORD o2 = mo | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(co, o2);
    GetConsoleMode(co, &mo);
    printf("    给 CONOUT$ 开 VT 位后读回:0x%04lx(VIRTUAL_TERMINAL_PROCESSING=%d,默认是关的)\n",
           (unsigned long)mo, (int)!!(mo & ENABLE_VIRTUAL_TERMINAL_PROCESSING));
    SetConsoleMode(co, o2 & ~ENABLE_VIRTUAL_TERMINAL_PROCESSING); // 还回去
    printf("输入 CP=%lu 输出 CP=%lu(本机 936,GBK)\n", (unsigned long)GetConsoleCP(),
           (unsigned long)GetConsoleOutputCP());
    return 0;
}
