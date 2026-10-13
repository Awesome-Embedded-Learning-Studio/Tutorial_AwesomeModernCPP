// E1:pty 四步手搓——posix_openpt / grantpt / unlockpt / ptsname
// 顺带盘 /dev/ptmx 与 devpts 在 WSL2 的实况:
//   - 谁是 ptmx(设备号)、devpts 挂载参数(ptmxmode=000 意味着什么)
//   - slave 节点什么时候出现在 /dev/pts、权限多少、master 关掉后节点去哪了
//   - unlockpt 之前 open slave 报什么错
//   - openpty 一个调用等于这四步的哪几步
#define _XOPEN_SOURCE 600
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <termios.h>
#include <unistd.h>

static void list_pts(const char* tag) {
    printf("  /dev/pts 内容(%s):", tag);
    DIR* d = opendir("/dev/pts");
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.')
            continue;
        printf(" %s", e->d_name);
    }
    closedir(d);
    printf("\n");
}

static void stat_node(const char* path) {
    struct stat st{};
    if (stat(path, &st) != 0) {
        printf("  stat %s 失败:%s\n", path, strerror(errno));
        return;
    }
    printf("  stat %s: uid=%u gid=%u mode=%04o\n", path, st.st_uid, st.st_gid, st.st_mode & 07777);
}

int main() {
    // 0. 环境实况
    struct stat mx{};
    stat("/dev/ptmx", &mx);
    printf("[0] 环境:/dev/ptmx 主次设备号 %u:%u(mode %04o);\n", major(mx.st_rdev),
           minor(mx.st_rdev), mx.st_mode & 07777);
    FILE* m = fopen("/proc/mounts", "r");
    char line[512];
    while (fgets(line, sizeof line, m))
        if (strstr(line, "/dev/pts") != nullptr)
            printf("    devpts 挂载:%s", line);
    fclose(m);

    list_pts("开新 pty 前");

    // 1. posix_openpt:拿 master
    int mfd = posix_openpt(O_RDWR | O_NOCTTY);
    printf("[1] posix_openpt -> master fd=%d\n", mfd);
    char* sn = ptsname(mfd);
    printf("    ptsname -> %s(isatty(master)=%d)\n", sn ? sn : "(null)", isatty(mfd));
    list_pts("posix_openpt 之后、grantpt 之前");

    // 2. unlockpt 之前硬开 slave:实测报什么
    int early = open(sn, O_RDWR | O_NOCTTY);
    printf("[2] unlockpt 之前 open(%s) -> %d errno=%d(%s)\n", sn, early, early < 0 ? errno : 0,
           early < 0 ? strerror(errno) : "成功");
    if (early >= 0)
        close(early);

    // 3. grantpt + unlockpt
    grantpt(mfd);
    printf("[3] grantpt 之后");
    stat_node(sn);
    unlockpt(mfd);
    printf("    unlockpt 之后");
    stat_node(sn);

    // 4. 开 slave
    int sfd = open(sn, O_RDWR | O_NOCTTY);
    printf("[4] open(%s) -> %d isatty=%d ttyname=%s\n", sn, sfd, isatty(sfd), ttyname(sfd));
    list_pts("master 开着");

    // 5. 写通一条:master 进、slave 出(过行规程,canonical 等行)
    write(mfd, "ping\n", 5);
    char buf[64];
    ssize_t n = read(sfd, buf, sizeof buf);
    printf("[5] master 写 \"ping\\n\",slave read -> n=%zd (%.*s):字节过了一遍行规程\n", n, (int)n,
           buf);

    // 6. 关 master,节点与 slave 的下场
    close(mfd);
    usleep(50 * 1000);
    printf("[6] close(master) 之后");
    stat_node(sn);
    errno = 0;
    n = read(sfd, buf, sizeof buf);
    printf("    slave 再 read -> n=%zd %s  <- master 没了,slave 读到 EOF(read 返回 0)\n", n,
           n == 0  ? "(EOF)"
           : n < 0 ? strerror(errno)
                   : "");
    errno = 0;
    ssize_t w = write(sfd, "x", 1);
    printf("    slave 再 write -> n=%zd %s  <- 写也一样,对端没人了\n", w,
           w < 0 ? strerror(errno) : "");
    close(sfd);
    list_pts("master 关掉后");
    return 0;
}
