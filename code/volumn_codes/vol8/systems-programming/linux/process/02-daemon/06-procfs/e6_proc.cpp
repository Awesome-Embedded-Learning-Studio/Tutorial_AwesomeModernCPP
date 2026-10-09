// E6 /proc/pid 家族速览:status 的 State/Uid、cmdline(NUL 分隔 = exec 时的 argv)、exe 链接
//   自查(self) + 外查(一个子进程的三种状态:活着 S / 死了没收尸 Z / 收尸后消失)
//   State 七态表(R R/S/D/Z/T/t/X)是文档口径,本实验抓到 R/S/Z 三态的实证
// 编译: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o e6_proc e6_proc.cpp
// 运行: ./e6_proc hello world
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

static void print_status_fields(const char* path, const char* who) {
    FILE* f = fopen(path, "r");
    if (!f) {
        printf("[%s] 打不开 %s: %s\n", who, path, strerror(errno));
        return;
    }
    char line[256];
    printf("[%s]\n", who);
    static const char* keys[] = {
        "Name:", "State:", "Tgid:", "Pid:", "PPid:", "Uid:", "Gid:", "Threads:"};
    while (fgets(line, sizeof line, f)) {
        for (const char* k : keys) {
            if (strncmp(line, k, strlen(k)) == 0) {
                fputs(line, stdout);
                break;
            }
        }
    }
    fclose(f);
}

static void dump_cmdline(pid_t pid, const char* who) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/cmdline", (int)pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("[%s] 打不开 %s: %s\n", who, path, strerror(errno));
        return;
    }
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) {
        printf("[%s] cmdline 空(僵尸/内核线程;内核线程的 cmdline 本来就是空)\n", who);
        return;
    }
    buf[n] = '\0';
    printf("[%s] /proc/%d/cmdline 共 %zd 字节, NUL 显示为 \\0:\n    \"", who, (int)pid, n);
    for (ssize_t i = 0; i < n; ++i) {
        if (buf[i]) {
            putchar(buf[i]);
        } else {
            putchar('\\');
            putchar('0');
        }
    }
    printf("\"\n    这就是 exec 时内核原样记下的 argv 各项,逐项以 NUL 拼接\n");
}

static void dump_exe(pid_t pid) {
    char path[64], link[512];
    snprintf(path, sizeof path, "/proc/%d/exe", (int)pid);
    ssize_t n = readlink(path, link, sizeof link - 1);
    if (n < 0) {
        printf("/proc/%d/exe: 读不到(%s)\n", (int)pid, strerror(errno));
        return;
    }
    link[n] = '\0';
    printf("/proc/%d/exe -> %s\n", (int)pid, link);
    printf("    (指向正在运行的二进制;文件被 rm 后这里会变成 \"... (deleted)\")\n");
}

int main(int argc, char** argv) {
    printf("== E6 /proc/pid 家族速览 ==\n");
    printf("argv: argc=%d, argv[0]=%s argv[1]=%s argv[2]=%s\n\n", argc, argv[0],
           argc > 1 ? argv[1] : "(无)", argc > 2 ? argv[2] : "(无)");

    printf("-- self 视角 --\n");
    print_status_fields("/proc/self/status", "self status(此刻在运行,State 应为 R)");
    dump_cmdline(getpid(), "self");
    dump_exe(getpid());

    printf("\n-- 外查一个子进程的状态变化 --\n");
    pid_t c = fork();
    if (c == 0) {
        sleep(1); // 活 1 秒然后 _exit(3)
        _exit(3);
    }
    usleep(200000);
    char p[64];
    snprintf(p, sizeof p, "/proc/%d/status", (int)c);
    print_status_fields(p, "子进程活着(在 sleep): State 应为 S(可中断睡眠)");

    sleep(2); // 子已 _exit(3),但父还没 waitpid -> 它停在 Z
    print_status_fields(p, "子进程已死未收尸(waitpid 前): State 应为 Z(僵尸)");

    dump_cmdline(c, "僵尸的 cmdline(应为空:地址空间已释放)");

    int st = 0;
    waitpid(c, &st, 0);
    printf("waitpid 后: 退出码=%d,再看 /proc/%d/status:\n", WEXITSTATUS(st), (int)c);
    printf("[%s] ", "收尸之后");
    FILE* g = fopen(p, "r");
    if (g) {
        printf("竟然还在(异常)\n");
        fclose(g);
    } else {
        printf("打开失败: %s —— 目录随收尸蒸发\n", strerror(errno));
    }
    printf("\nUid 四元组: 依次是 real/effective/saved/fs —— 本实验全程普通用户,四项相同;\n");
    printf("setuid 程序里 euid 会先变,后续 exec 时 saved 决定\"还能不能变回去\"\n");
    return 0;
}
