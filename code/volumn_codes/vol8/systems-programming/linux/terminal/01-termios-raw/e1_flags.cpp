// E1: termios 四组标志普查 + cfmakeraw 与手工清位的差异
// 安全口径:实验全程跑在自建的 pty 里,主终端一个字节都不动。
//   [A] openpty 一对:master/slave 谁是 tty 的判据,master 端能不能 tcgetattr/tcsetattr(Linux
//   实测能,且与 slave 同步) [B] 新开 pty 的出厂 termios 逐位翻成人话(四组标志 + c_cc
//   特殊字符),波特率的记录方式单列 [C] cfmakeraw 前后 diff / 手工只清 ICANON|ECHO 的"最小 raw"
//   diff,两者差在哪
#include <fcntl.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

struct BitName {
    tcflag_t bit;
    const char* name;
};

static const BitName kIflag[] = {
    {IGNBRK, "IGNBRK"}, {BRKINT, "BRKINT"}, {IGNPAR, "IGNPAR"}, {PARMRK, "PARMRK"},
    {INPCK, "INPCK"},   {ISTRIP, "ISTRIP"}, {INLCR, "INLCR"},   {IGNCR, "IGNCR"},
    {ICRNL, "ICRNL"},   {IXON, "IXON"},
};
static const BitName kOflag[] = {
    {OPOST, "OPOST"}, {ONLCR, "ONLCR"}, {OCRNL, "OCRNL"}, {ONOCR, "ONOCR"}, {ONLRET, "ONLRET"},
};
static const BitName kCflag[] = {
    {CSTOPB, "CSTOPB"}, {CREAD, "CREAD"}, {PARENB, "PARENB"},
    {PARODD, "PARODD"}, {HUPCL, "HUPCL"}, {CLOCAL, "CLOCAL"},
};
static const BitName kLflag[] = {
    {ISIG, "ISIG"},       {ICANON, "ICANON"},   {ECHO, "ECHO"},     {ECHOE, "ECHOE"},
    {ECHOK, "ECHOK"},     {ECHONL, "ECHONL"},   {NOFLSH, "NOFLSH"}, {TOSTOP, "TOSTOP"},
    {ECHOCTL, "ECHOCTL"}, {ECHOPRT, "ECHOPRT"}, {ECHOKE, "ECHOKE"}, {IEXTEN, "IEXTEN"},
};

static void dump_group(const char* title, tcflag_t v, const BitName* tab, size_t n) {
    const char* cs = nullptr; // CSIZE 是多选一的字段,单独解析,避免 CS7/CS8 一起误印
    if (std::strcmp(title, "c_cflag") == 0) {
        switch (v & CSIZE) {
            case CS6:
                cs = "CS6";
                break;
            case CS7:
                cs = "CS7";
                break;
            case CS8:
                cs = "CS8";
                break;
            default:
                cs = "CS5";
                break;
        }
        v &= ~(tcflag_t)CSIZE;
    }
    std::printf("  %-8s = 0x%08lx : %s", title, (unsigned long)(cs ? (v | CSIZE) : v),
                cs ? cs : "");
    for (size_t i = 0; i < n; ++i)
        if (v & tab[i].bit)
            std::printf(" %s", tab[i].name);
    std::printf("\n");
}

static void dump_cc(const termios& t) {
    auto printable = [](unsigned char c) -> std::string {
        if (c == 0)
            return "^@";
        if (c < 32) {
            char b[8];
            std::snprintf(b, sizeof b, "^%c", c + '@');
            return b;
        }
        if (c == 127)
            return "^?";
        char b[8];
        std::snprintf(b, sizeof b, "'%c'", c);
        return b;
    };
    std::printf("  c_cc: VINTR=%s VQUIT=%s VERASE=%s VKILL=%s VEOF=%s VSTART=%s VSTOP=%s "
                "VSUSP=%s VMIN=%u VTIME=%u\n",
                printable(t.c_cc[VINTR]).c_str(), printable(t.c_cc[VQUIT]).c_str(),
                printable(t.c_cc[VERASE]).c_str(), printable(t.c_cc[VKILL]).c_str(),
                printable(t.c_cc[VEOF]).c_str(), printable(t.c_cc[VSTART]).c_str(),
                printable(t.c_cc[VSTOP]).c_str(), printable(t.c_cc[VSUSP]).c_str(),
                (unsigned)t.c_cc[VMIN], (unsigned)t.c_cc[VTIME]);
}

static void dump(const char* tag, const termios& t) {
    std::printf("[%s]\n", tag);
    dump_group("c_iflag", t.c_iflag, kIflag, sizeof kIflag / sizeof kIflag[0]);
    dump_group("c_oflag", t.c_oflag, kOflag, sizeof kOflag / sizeof kOflag[0]);
    dump_group("c_cflag", t.c_cflag, kCflag, sizeof kCflag / sizeof kCflag[0]);
    dump_group("c_lflag", t.c_lflag, kLflag, sizeof kLflag / sizeof kLflag[0]);
    dump_cc(t);
}

static void diff_group(const char* title, tcflag_t a, tcflag_t b, const BitName* tab, size_t n) {
    std::printf("  %-8s:", title);
    for (size_t i = 0; i < n; ++i)
        if ((a & tab[i].bit) != (b & tab[i].bit))
            std::printf(" %s%s", (b & tab[i].bit) ? "+" : "-", tab[i].name);
    if (a == b)
        std::printf(" (无变化)");
    std::printf("\n");
}

static void diff(const char* tag, const termios& a, const termios& b) {
    std::printf("[%s 相对原始的差异]\n", tag);
    diff_group("c_iflag", a.c_iflag, b.c_iflag, kIflag, sizeof kIflag / sizeof kIflag[0]);
    diff_group("c_oflag", a.c_oflag, b.c_oflag, kOflag, sizeof kOflag / sizeof kOflag[0]);
    diff_group("c_cflag", a.c_cflag, b.c_cflag, kCflag, sizeof kCflag / sizeof kCflag[0]);
    diff_group("c_lflag", a.c_lflag, b.c_lflag, kLflag, sizeof kLflag / sizeof kLflag[0]);
}

int main() {
    int mfd, sfd;
    if (openpty(&mfd, &sfd, nullptr, nullptr, nullptr) != 0) {
        perror("openpty");
        return 1;
    }

    // [A] master 与 slave 的身份
    termios tm{}, ts{};
    int gm = tcgetattr(mfd, &tm), gs = tcgetattr(sfd, &ts);
    std::printf("[A] master=%d slave=%d:isatty(master)=%d isatty(slave)=%d\n", mfd, sfd,
                isatty(mfd), isatty(sfd));
    std::printf("    tcgetattr(master)=%d tcgetattr(slave)=%d 两端看到的 iflag/lflag 相同:%d\n", gm,
                gs, tm.c_iflag == ts.c_iflag && tm.c_lflag == ts.c_lflag);
    tm.c_lflag &= ~(tcflag_t)ECHO;
    int sm = tcsetattr(mfd, TCSANOW, &tm);
    termios ts2{};
    tcgetattr(sfd, &ts2);
    std::printf("    经 master 清 ECHO:tcsetattr(master)=%d,slave 侧 "
                "ECHO=%d(0=同步清掉,两端共享同一份行规程)\n",
                sm, (int)!!(ts2.c_lflag & ECHO));
    ts2.c_lflag |= ECHO; // 恢复,不影响后续
    tcsetattr(sfd, TCSANOW, &ts2);

    // [B] 出厂设置普查
    termios orig{};
    tcgetattr(sfd, &orig);
    dump("B1. 新开 pty slave 的出厂设置(内核默认)", orig);
    std::printf("    c_cflag=0x%lx 的记录:低 4 位 0x%lx 是输出波特率的老编码(B38400=0o17),"
                "0x%lx 那几位是输入波特率的镜像(老编码<<16);本机 glibc 的 B 常量已是真实速率值"
                "(B9600 宏==9600,glibc 2.42 起的新口径),内核里存的仍是老编码,glibc "
                "两头翻译,round-trip 实测:\n",
                (unsigned long)orig.c_cflag, (unsigned long)(orig.c_cflag & 0xf),
                (unsigned long)(orig.c_cflag & 0xf0000));
    termios b9600 = orig;
    cfsetospeed(&b9600, B9600);
    cfsetispeed(&b9600, B9600);
    tcsetattr(sfd, TCSANOW, &b9600);
    termios chk{};
    tcgetattr(sfd, &chk);
    std::printf("    设 B9600 后 cfgetospeed=%lu c_cflag=0x%lx(低 4 位变 0xd=0o15 老编码,pty "
                "上无物理意义,stty 同样报 9600)\n",
                (unsigned long)cfgetospeed(&chk), (unsigned long)chk.c_cflag);
    tcsetattr(sfd, TCSANOW, &orig); // 还回 38400 原状

    // [C] 三份对照
    termios raw = orig;
    cfmakeraw(&raw);
    dump("C1. cfmakeraw 之后", raw);
    diff("cfmakeraw", orig, raw);
    std::printf("    注意:cfmakeraw 只清 OPOST、不清 ONLCR(OPOST 一关,ONLCR 失去作用,留着无害);"
                "ECHOE/ECHOK/ECHOCTL/ECHOKE 是 ECHO 的子开关,母开关 ECHO 一清它们全部失效;\n"
                "    本例 CSIZE 已是 CS8,所以 c_cflag 看不出变化。\n");

    termios manual = orig;
    manual.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
    dump("C2. 手工只清 ICANON|ECHO(常见的『最小 raw』)", manual);
    diff("C2. 手工最小 raw", orig, manual);

    std::printf("[C3. 手工最小 raw 与 cfmakeraw 的差距]\n");
    diff("手工 raw vs cfmakeraw", manual, raw);
    std::printf("    含义:手工版下 ISIG 仍在(^C 还是信号)、IXON 仍在(^S/^Q 会被吃掉)、ICRNL "
                "仍在(CR 会被改写成 NL)、\n"
                "             OPOST 仍在(程序写的 \\n 出门变 \\r\\n)。这些差异正是 E2/E3/E5 "
                "要逐个看到的行为。\n");
    return 0;
}
