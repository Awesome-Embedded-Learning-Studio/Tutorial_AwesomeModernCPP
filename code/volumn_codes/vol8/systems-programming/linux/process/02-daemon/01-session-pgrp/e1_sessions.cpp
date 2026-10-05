// E1 会话与进程组全景:getsid/getpgid/setpgid 家族 + ps 四列对照
//   - fork 后子 setpgid(0,0) 脱离父进程组(pgid 变,sid 不变)
//   - 进程组组长 setsid() -> EPERM;孙(非组长)setsid() 成新会话长
//   - 会话长再 setsid() -> 同样 EPERM(会话长必是组长)
// 编译: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o e1_sessions e1_sessions.cpp
// 运行: ./e1_sessions   (子/孙各存活 ~3s,期间父进程借 popen 抓 ps 快照)
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

static void dump(const char* tag) {
    printf("%-26s pid=%-7d ppid=%-7d pgid=%-7d sid=%-7d\n", tag, (int)getpid(), (int)getppid(),
           (int)getpgid(0), (int)getsid(0));
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0); // 子进程 _exit 不刷 stdio 缓冲,先关缓冲再 fork
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        perror("pipe");
        return 1;
    }

    printf("== E1 会话与进程组全景 ==\n");
    dump("初始(本进程)");
    printf("跨进程查询家族: getsid(pid)/getpgid(pid) 对任意进程可用 ->\n");
    printf("  getsid(1)=%d  getpgid(1)=%d  (PID 1 的会话与进程组)\n", (int)getsid(1),
           (int)getpgid(1));
    printf("  getsid(0)=%d  getpgid(0)=%d  (0 = 自己,与上面 dump 里一致)\n\n", (int)getsid(0),
           (int)getpgid(0));

    int shell_pid = (int)getppid(); // 外层 shell,ps 快照里对照用

    pid_t a = fork();
    if (a == 0) { // 子A
        close(pipefd[0]);
        dump("子A: fork 后");

        if (setpgid(0, 0) != 0)
            perror("setpgid");
        dump("子A: setpgid(0,0) 后"); // pgid 变成自己,sid 不动

        // 组长身份调 setsid -> EPERM(setsid 要求调用者不是进程组组长)
        errno = 0;
        if (setsid() == (pid_t)-1)
            printf("子A(现组长,pid==pgid): setsid() = -1, errno=%d = %s\n", errno, strerror(errno));

        pid_t g = fork(); // 孙
        if (g == 0) {
            close(pipefd[1]);
            errno = 0;
            pid_t ns = setsid(); // 孙不是组长(继承了子A的 pgid,pid!=pgid)
            if (ns == (pid_t)-1) {
                printf("孙: setsid 失败: %s\n", strerror(errno));
            } else {
                printf("孙: setsid() = %d -> 成为新会话的会话长兼新组长\n", (int)ns);
                dump("孙: setsid 后");
                errno = 0;
                if (setsid() == (pid_t)-1)
                    printf("孙(已是会话长): setsid() = -1, errno=%d = %s\n", errno,
                           strerror(errno));
            }
            sleep(3);
            _exit(0);
        }
        // 把孙的 pid 报给祖父,供 ps 快照
        ssize_t wr = write(pipefd[1], &g, sizeof g);
        (void)wr;
        close(pipefd[1]);
        waitpid(g, nullptr, 0);
        _exit(0);
    }
    close(pipefd[1]);
    pid_t g = -1;
    ssize_t rd = read(pipefd[0], &g, sizeof g);
    (void)rd;
    close(pipefd[0]);
    usleep(200000); // 等子A的 setpgid/孙的 setsid 都落地

    printf("\n== ps 快照对照(pid,ppid,pgid,sid) ==\n");
    printf("API 读数: 本进程 pgid=%d sid=%d | 子A pgid=%d sid=%d | 孙 pgid=%d sid=%d\n",
           (int)getpgid(0), (int)getsid(0), (int)getpgid(a), (int)getsid(a),
           g > 0 ? (int)getpgid(g) : -1, g > 0 ? (int)getsid(g) : -1);
    printf("四列怎么看: 子A 只有 PGID 列离开父亲;孙的 PGID 和 SID 都等于自己 pid\n\n");
    char cmd[256];
    snprintf(cmd, sizeof cmd, "ps -o pid,ppid,pgid,sid,comm -p %d,%d,%d,%d", shell_pid,
             (int)getpid(), (int)a, (int)g);
    FILE* p = popen(cmd, "r");
    if (p) {
        char line[256];
        while (fgets(line, sizeof line, p))
            fputs(line, stdout);
        pclose(p);
    } else {
        perror("popen ps");
    }
    waitpid(a, nullptr, 0);
    return 0;
}
