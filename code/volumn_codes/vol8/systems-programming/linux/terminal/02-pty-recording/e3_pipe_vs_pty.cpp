// E3:同一串字节、同一个读程序,喂管道与喂 pty,到手的东西不一样
//   场景1  管道:字节原样透传,"abc DEL z \n" 六个字节一个不少
//   场景2  pty(canonical+ECHO 出厂态):到手 "abz\n" 四个字节——行编辑是 tty 层做的;
//          master 里另收到一份回显(DEL 回显成退格-空格-退格)
//   场景3  pty 关 ECHO:到手还是编辑过的 "abz\n"(编辑不依赖回显),master 里没有回显
// 读程序是同一个:本程序带 "reader" 参数重入自己,读 stdin,每次 read 上报到 fd3 的报告管道。
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

static std::string hexof(const char* p, size_t n) {
    std::string s;
    char b[8];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof b, "%02x ", (unsigned char)p[i]);
        s += b;
    }
    s += "(";
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)p[i];
        if (c == '\n')
            s += "\\n";
        else if (c == '\r')
            s += "\\r";
        else if (c == 0x7f)
            s += "DEL";
        else if (c == 0x08)
            s += "^H";
        else if (c < 32) {
            std::snprintf(b, sizeof b, "^%c", c + '@');
            s += b;
        } else
            s += (char)c;
    }
    return s + ")";
}

static int run_reader(int report_w) { // 孩子重入:stdin 有什么读什么,上报 fd3
    char buf[512];
    for (;;) {
        ssize_t n = read(0, buf, sizeof buf);
        char msg[600];
        int len = std::snprintf(msg, sizeof msg, "READ n=%zd %s\n", n,
                                n > 0 ? hexof(buf, (size_t)n).c_str() : "");
        if (write(report_w, msg, (size_t)len) < 0)
            break;
        if (n <= 0)
            break;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "reader") == 0)
        return run_reader(3);

    const char* payload = "abc\x7fz\n"; // 6 字节:三个字母、一个 DEL、一个字母、换行

    // ---- 场景1:管道 ----
    {
        int in_p[2], rp[2];
        pipe(in_p);
        pipe(rp);
        pid_t pid = fork();
        if (pid == 0) {
            dup2(in_p[0], 0);
            close(in_p[0]);
            close(in_p[1]);
            dup2(rp[1], 3);
            close(rp[0]);
            close(rp[1]);
            execl("/proc/self/exe", "e3", "reader", (char*)nullptr);
            _exit(127);
        }
        close(in_p[0]);
        close(rp[1]);
        write(in_p[1], payload, std::strlen(payload));
        close(in_p[1]);
        std::printf("[场景1 管道]喂 %s\n", hexof(payload, std::strlen(payload)).c_str());
        char b[600];
        ssize_t n;
        while ((n = read(rp[0], b, sizeof b)) > 0)
            std::printf("  %.*s", (int)n, b);
        waitpid(pid, nullptr, 0);
        close(rp[0]);
        std::printf("  —— 6 个字节一个不少,DEL 就是 0x7f,没有谁替你编辑\n\n");
    }

    // ---- 场景2/3:pty ----
    for (int variant = 0; variant < 2; ++variant) {
        int rp[2];
        pipe(rp);
        int mfd = -1;
        pid_t pid = forkpty(&mfd, nullptr, nullptr, nullptr);
        if (pid == 0) {
            close(mfd);
            close(rp[0]);
            dup2(rp[1], 3);
            close(rp[1]);
            execl("/proc/self/exe", "e3", "reader", (char*)nullptr);
            _exit(127);
        }
        close(rp[1]);
        if (variant == 1) { // 场景3:关 ECHO
            termios t{};
            tcgetattr(mfd, &t);
            t.c_lflag &= ~(tcflag_t)ECHO;
            tcsetattr(mfd, TCSANOW, &t);
        }
        std::printf(variant == 0 ? "[场景2 pty 出厂态(canonical+ECHO)]\n"
                                 : "[场景3 pty 关 ECHO(编辑仍在)]\n");
        usleep(30 * 1000);
        write(mfd, payload, std::strlen(payload));
        usleep(100 * 1000);
        // 抽 master
        fcntl(mfd, F_SETFL, O_NONBLOCK);
        std::string echo;
        char b[512];
        ssize_t n;
        while ((n = read(mfd, b, sizeof b)) > 0)
            echo += hexof(b, (size_t)n);
        std::printf("  喂 %s\n", hexof(payload, std::strlen(payload)).c_str());
        std::printf("  master 收到的回显:%s\n", echo.empty() ? "(无)" : echo.c_str());
        // 关 master 结束 reader(EOF)
        fcntl(mfd, F_SETFL, 0);
        close(mfd);
        char msg[600];
        while ((n = read(rp[0], msg, sizeof msg)) > 0)
            std::printf("  %.*s", (int)n, msg);
        waitpid(pid, nullptr, 0);
        close(rp[0]);
        std::printf("  —— 到手的是编辑后的行;行编辑、回显都在 tty 层,不在读程序里%s\n\n",
                    variant == 0 ? "" : ";回显可以单独关掉,编辑照做");
    }
    return 0;
}
