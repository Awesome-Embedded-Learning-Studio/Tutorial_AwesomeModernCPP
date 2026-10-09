// E2 守护进程化的经典步骤(重头戏):fork -> setsid -> fork -> chdir("/") -> umask(0) -> stdio 重定向
// /dev/null
//   每一步的 pid/ppid/pgid/sid/fd0/控制终端 全部落进 trace 文件——
//   因为 stdout 后面要被重定向进 /dev/null,trace 必须走另一条专用 fd
// 编译: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o e2_daemon_steps e2_daemon_steps.cpp
// 运行(要给 fd0/1/2 一个真终端,才能看到 tty 变化;script 就是借个 pty):
//   script -qec './e2_daemon_steps e2_daemon_steps.out; sleep 1' /dev/null
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
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static int tfd = -1;

static void t(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vdprintf(tfd, fmt, ap);
    va_end(ap);
}

// 控制终端的判据:ioctl(fd0, TIOCGSID) 成功 <=> fd0 是"本会话的控制终端"
// (fd0 是个 tty 但不属于本会话时,同样失败——setsid 切断的正是这层"归属")
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

    t("%-32s pid=%-6d ppid=%-6d pgid=%-6d sid=%-6d\n"
      "    fd0 -> %s ; 控制终端:%s\n",
      stage, (int)getpid(), (int)getppid(), (int)getpgid(0), (int)getsid(0), link, ctty);
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "e2_daemon_steps.out";
    tfd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (tfd < 0) {
        perror("open trace");
        return 1;
    }

    int syncfd[2];
    if (pipe(syncfd) != 0) {
        perror("pipe");
        return 1;
    }

    t("== E2 守护进程化经典步骤:sid/pgid/tty 变化链 ==\n\n");
    snapshot("阶段0 初始");

    pid_t c1 = fork();
    if (c1 < 0) {
        perror("fork1");
        return 1;
    }
    if (c1 > 0) {
        // 父进程等子进程 setsid 完成的通知再退:演示环境的外层 script 会在本进程退出时
        // 拆掉 pty,若那时子进程还没脱离会话,会被连带 SIGHUP 误杀(真实 shell 场景里
        // 父进程 fork 完立刻退,shell 活着,不存在这个坑)
        char b = 0;
        ssize_t rd = read(syncfd[0], &b, 1);
        (void)rd;
        close(syncfd[0]);
        t("\n(父进程 pid=%d:收到\"子已 setsid\"的通知,现在退出。\n", (int)getpid());
        t(" 第一次 fork 的意义:shell 只等父进程——父一退,shell 就认为命令结束了,\n");
        t(" 而真正的服务在子进程里继续跑,从此与 shell 的生命周期解耦)\n");
        _exit(0);
    }
    close(syncfd[0]);

    snapshot("阶段1 fork#1 后(子,父暂未退)");

    if (setsid() == (pid_t)-1) {
        t("setsid 失败: %s\n", strerror(errno));
        _exit(1);
    }
    t("\nsetsid() 成功,三条实证:\n");
    t("  1) pid=%d pgid=%d sid=%d -> pid==pgid==sid,本进程是新会话的会话长兼新组长\n",
      (int)getpid(), (int)getpgid(0), (int)getsid(0));
    t("  2) TIOCGSID 从成功变 ENOTTY:控制终端归属被切断\n");
    t("  3) fd0 的 readlink 没变:描述符还开着,指向旧 tty——所以下一步还要显式重定向\n");
    snapshot("阶段2 setsid 后");

    char b = 'd';
    ssize_t wr = write(syncfd[1], &b, 1);
    (void)wr;
    close(syncfd[1]);
    usleep(300000); // 等父退出、收养生效
    t("\n(父进程已退,getppid()=%d:孤儿被收养。教科书口径是 1/init,本机取决于最近的 subreaper)\n",
      (int)getppid());

    pid_t c2 = fork();
    if (c2 < 0) {
        perror("fork2");
        _exit(1);
    }
    if (c2 > 0) {
        t("\n(会话长 pid=%d 退出,孙 pid=%d 继续。\n", (int)getpid(), (int)c2);
        t(" 第二次 fork 的意义:孙不是会话长,永远无法通过 open(tty 设备)自动获得\n");
        t(" 控制终端——有的终端实现会把它送给第一个 open 它的会话长,双 fork 把这条路堵死)\n");
        _exit(0);
    }
    usleep(300000);
    snapshot("阶段3 fork#2 后(孙)");

    char cwd[4096];
    if (getcwd(cwd, sizeof cwd))
        t("\nchdir 前 cwd=%s\n", cwd);
    chdir("/");
    if (getcwd(cwd, sizeof cwd))
        t("chdir(\"/\") 后 cwd=%s(不占着挂载点,不妨碍别人卸载文件系统)\n", cwd);

    mode_t old = umask(0);
    t("umask: %04o -> 0000(守护进程要把新建文件的权限位完全交给自己代码里的 open mode)\n",
      (unsigned)old);

    int dn = open("/dev/null", O_RDWR);
    if (dn >= 0) {
        dup2(dn, STDIN_FILENO);
        dup2(dn, STDOUT_FILENO);
        dup2(dn, STDERR_FILENO);
        if (dn > STDERR_FILENO)
            close(dn);
    }
    snapshot("阶段6 stdio 重定向后");
    t("    isatty(0)=%d(fd0/1/2 全部指向 /dev/null:读永远 EOF,写永远无声无息)\n", isatty(0));

    t("\n[DONE] 最终画像: pid=%d ppid=%d pgid=%d sid=%d\n", (int)getpid(), (int)getppid(),
      (int)getpgid(0), (int)getsid(0));
    t("  对照断言: sid!=pid(非会话长)且 pgid!=pid(非组长)且无控制终端 -> 守护进程雏形成立\n");
    return 0;
}
