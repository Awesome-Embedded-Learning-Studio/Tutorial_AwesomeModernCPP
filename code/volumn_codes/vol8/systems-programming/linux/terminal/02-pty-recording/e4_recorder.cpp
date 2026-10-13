// E4:script(1) 的最小复刻——录制一场 pty 会话
// 思路跟 script 一模一样:forkpty 开台,孩子 exec 交互式 sh,父进程扮演"终端仿真器":
//   - 往 master 写 = 键盘敲进去的(会被回显、会被行编辑)
//   - 从 master 读 = 屏幕该显示的字节(回显 + 程序输出混在同一条流里)
// 录制产物两份:
//   session.typescript  原始字节流(master 读到的每一笔追加)
//   session.timing      每笔一行的时刻表:距上一笔的毫秒 + 这笔的字节数
// 输入日程是自动的(本实验没有真人敲键盘):echo hello → sleep 1; echo done → exit。
// 关键观察:master 这一条流里,「我敲的字」与「程序打的字」无法区分——录制物没有方向信息。
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

static double now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main() {
    FILE* script = fopen("session.typescript", "wb");
    FILE* timing = fopen("session.timing", "wt");
    if (!script || !timing) {
        perror("fopen");
        return 1;
    }

    int mfd = -1;
    pid_t pid = forkpty(&mfd, nullptr, nullptr, nullptr);
    if (pid < 0) {
        perror("forkpty");
        return 1;
    }
    if (pid == 0) {
        execl("/bin/sh", "sh", "-i", (char*)nullptr);
        _exit(127);
    }

    // 设一个窗口尺寸,免得 0x0 的 winsize 让 sh 觉得没有终端能力
    struct winsize wz{24, 80, 0, 0};
    ioctl(mfd, TIOCSWINSZ, &wz);

    const char* script_lines[] = {"echo hello\n", "sleep 1; echo done\n", "exit\n"};
    int stage = 0; // 0:等 shell 起来再发第一条
    double t_last = now_ms();
    double t_start = t_last;
    std::string seen; // 全量流,用于搜输出关键字
    size_t total = 0, chunks = 0;
    bool fed[3] = {false, false, false};

    struct pollfd pfd{mfd, POLLIN, 0};
    char buf[4096];
    for (;;) {
        int sent = 0; // 这轮要不要发输入:第0条 t>200ms;第1条等到 hello 出现;第2条等到 done 出现
        double now = now_ms();
        if (stage == 0 && now - t_start > 200) {
            fed[0] = true;
            stage = 1;
            sent = 1;
        } else if (stage == 1 && seen.find("hello") != std::string::npos) {
            fed[1] = true;
            stage = 2;
            sent = 2;
        } else if (stage == 2 && seen.find("done") != std::string::npos) {
            fed[2] = true;
            stage = 3;
            sent = 3;
        }

        int r = poll(&pfd, 1, fed[2] ? 500 : 50);
        if (r < 0) {
            perror("poll");
            break;
        }
        if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = read(mfd, buf, sizeof buf);
            if (n <= 0) {
                std::printf("read(master) -> n=%zd %s —— 会话收场\n", n,
                            n < 0 ? strerror(errno) : "(EOF)");
                break;
            }
            double t = now_ms();
            fprintf(timing, "%.1f %zd\n", t - t_last, (size_t)n);
            t_last = t;
            fwrite(buf, 1, (size_t)n, script);
            seen.append(buf, (size_t)n);
            total += (size_t)n;
            ++chunks;
        }
        if (sent == 1)
            write(mfd, script_lines[0], std::strlen(script_lines[0]));
        if (sent == 2)
            write(mfd, script_lines[1], std::strlen(script_lines[1]));
        if (sent == 3)
            write(mfd, script_lines[2], std::strlen(script_lines[2]));
        if (fed[2]) {
            // exit 已发出;等 sh 退
            int st = 0;
            pid_t w = waitpid(pid, &st, WNOHANG);
            if (w == pid)
                break;
        }
    }
    waitpid(pid, nullptr, 0);
    fclose(script);
    fclose(timing);

    std::printf("录制完成:时长 %.0fms,%zu 笔共 %zu 字节\n", now_ms() - t_start, chunks, total);
    std::printf("typescript 开头 48 字节的十六进制(注意 0d 0a 与提示符):\n  ");
    FILE* f = fopen("session.typescript", "rb");
    unsigned char head[48];
    size_t hn = fread(head, 1, sizeof head, f);
    fclose(f);
    for (size_t i = 0; i < hn; ++i)
        std::printf("%02x ", head[i]);
    std::printf("\n  可见形式:");
    for (size_t i = 0; i < hn; ++i)
        std::printf("%s", head[i] == '\r'   ? "\\r"
                          : head[i] == '\n' ? "\\n\n  "
                                            : std::string(1, (char)head[i]).c_str());
    std::printf("\n");
    std::printf("timing 文件(前 8 行,格式:距上一笔的毫秒 这笔字节数):\n");
    f = fopen("session.timing", "rt");
    char line[64];
    for (int i = 0; i < 8 && fgets(line, sizeof line, f); ++i)
        std::printf("  %s", line);
    fclose(f);
    return 0;
}
