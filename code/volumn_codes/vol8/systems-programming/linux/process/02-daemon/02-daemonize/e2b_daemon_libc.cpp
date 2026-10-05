// E2b libc 的 daemon(0,0) 一步到位,与手工双 fork 对照
//   daemon() = fork + setsid(+chdir+重定向),只 fork 一次
//   -> 返回后 pid==pgid==sid,本进程仍是会话长:之后 open 一个 tty 设备(不带 O_NOCTTY)
//      仍可能把它抓成控制终端——这正是手工版要第二次 fork 的原因
// 编译: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o e2b_daemon_libc e2b_daemon_libc.cpp
// 运行: script -qec './e2b_daemon_libc e2b_daemon_libc.out; sleep 1' /dev/null
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static int tfd = -1;

static void t(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vdprintf(tfd, fmt, ap);
    va_end(ap);
}

static void snapshot(const char* stage) {
    char link[256];
    ssize_t n = readlink("/proc/self/fd/0", link, sizeof link - 1);
    if (n < 0) {
        strcpy(link, "(readlink 失败)");
    } else {
        link[n] = '\0';
    }
    pid_t tsid = -1;
    errno = 0;
    int r = ioctl(STDIN_FILENO, TIOCGSID, &tsid);
    char ctty[96];
    if (r == 0)
        snprintf(ctty, sizeof ctty, "有,该 tty 所属会话 sid=%d", (int)tsid);
    else
        snprintf(ctty, sizeof ctty, "无,ioctl(fd0,TIOCGSID) 失败: %s", strerror(errno));
    t("%-28s pid=%-6d ppid=%-6d pgid=%-6d sid=%-6d\n"
      "    fd0 -> %s ; 控制终端:%s\n",
      stage, (int)getpid(), (int)getppid(), (int)getpgid(0), (int)getsid(0), link, ctty);
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "e2b_daemon_libc.out";
    tfd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (tfd < 0) {
        perror("open trace");
        return 1;
    }

    t("== E2b daemon(0,0) 一步到位对照 ==\n");
    t("(POSIX.1-2008 已废除 daemon();glibc 仍提供,同款 snapshot 看清楚它做了/没做什么)\n\n");
    snapshot("调用 daemon() 前");

    if (daemon(0, 0) != 0) { // 0=chdir("/"), 0=重定向 stdio 到 /dev/null
        t("daemon 失败: %s\n", strerror(errno));
        return 1;
    }
    snapshot("daemon() 返回后");

    char cwd[4096];
    if (getcwd(cwd, sizeof cwd))
        t("\ndaemon(0,0) 顺带做了 chdir(\"/\"):cwd=%s\n", cwd);
    t("对照手工双 fork 版(E2)的最终态: 那边 sid!=pid 且 pgid!=pid;\n");
    t("这里 pid=%d pgid=%d sid=%d -> pid==pgid==sid,本进程仍是会话长,\n", (int)getpid(),
      (int)getpgid(0), (int)getsid(0));
    t("只差一次 fork:若之后 open 一个终端设备(不带 O_NOCTTY),它还可能被抓成控制终端。\n");
    t("另外 daemon() 不屏蔽任何信号、不处理 SIGHUP——这些保险都得调用者自己加。\n");
    return 0;
}
