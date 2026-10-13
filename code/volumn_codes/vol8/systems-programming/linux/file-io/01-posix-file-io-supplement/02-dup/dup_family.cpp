// dup_family.cpp —— dup / dup2 / dup3 全家:重定向、共享偏移、CLOEXEC 过不过 exec
// (《POSIX 文件 I/O》补课段 E2)
//
// 观察点(对照 dup_family.out):
//   a) dup 拿最低空闲 fd;F_SETFD 补的 FD_CLOEXEC 不会被 dup 复制
//   b) 共享偏移:fd1 读一段,fd2(dup 出来的)接着往下读;重新 open 的 fd3 却从头读
//   c) dup2 原子重定向:1 号槽位换给文件,printf 落文件,再换回来;
//      stdio 缓冲区在用户态,没 fflush 的那条跟着「当时的 fd 1」走,不跟着调用时刻走
//   d) 为什么不能 close(1) 再 dup:close 和 dup 之间的空窗里,别的 open 会抢走 1 号槽
//   e) dup3 的两个差异:flags 参数能带 O_CLOEXEC;oldfd==newfd 直接 EINVAL(dup2 是无害 no-op)
//   f) fork+exec 下 CLOEXEC 的生死(本实验的主菜):
//        不带 FD_CLOEXEC 的 fd → 活过 execve,子进程 exec 后还能 read;
//        带 FD_CLOEXEC 的 fd   → exec 前被内核关掉,exec 后 fcntl(F_GETFD) 直接 EBADF;
//        从带 CLOEXEC 的 fd 复制出来的 fd → dup 不复制 fd 标志,又活了
//      证据用行为验证:exec 前后各列一次 /proc/self/fd,加上 exec 后的 read/fcntl 结果
//
// 跑法:直接跑 ./e2_dup。子进程通过 /proc/self/exe 重新 exec 自己,fd 号经 argv 传过去。
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE 1 // dup3 / O_CLOEXEC
#endif

#include "article.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

namespace {

const char* data_path = "/home/charliechen/l01b_scratch/e2/data.txt";
const char* redir_path = "/home/charliechen/l01b_scratch/e2/redirect.txt";
const char* junk_path = "/home/charliechen/l01b_scratch/e2/junk.txt";

void banner(const char* s) {
    std::printf("\n==== %s ====\n", s);
}

// 列 /proc/self/fd:exec 前后各调一次,哪些 fd 活过了 execve 一眼看清
void list_fds(const char* tag) {
    std::printf("%s /proc/self/fd:", tag);
    DIR* d = ::opendir("/proc/self/fd");
    if (!d) {
        std::printf(" opendir 失败:%s\n", std::strerror(errno));
        return;
    }
    // opendir 自己会占一个 fd(通常是目录里编号最大的那个),列完关掉
    int self = ::dirfd(d);
    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        char* end = nullptr;
        long n = std::strtol(e->d_name, &end, 10);
        if (*end != '\0' || n < 0)
            continue;
        if ((int)n != self)
            std::printf(" %ld", n);
    }
    std::printf("(pid=%d)\n", (int)::getpid());
    ::closedir(d);
}

} // namespace

// ---- exec 后的子进程走这里:argv = {self, "--child", fdPlain, fdCloexec, fdDup} ----
static int child_main(int fd_plain, int fd_cloexec, int fd_dup) {
    banner("f) exec 之后(子进程视角):同一批 fd 号,三种命运");
    list_fds("[child exec 后]");
    std::printf("(exec 前是 0 1 2 3 4 20;对照上面,4 没了、3 和 20 还在)\n");

    char buf[32];
    ssize_t n = ::read(fd_plain, buf, sizeof buf - 1);
    if (n >= 0)
        buf[n] = '\0';
    std::printf("read(%d, ...)          = %ld:\"%s\"  <- 裸 fd 活过了 execve\n", fd_plain, (long)n,
                n >= 0 ? buf : "");

    int g = ::fcntl(fd_cloexec, F_GETFD);
    std::printf(
        "fcntl(%d, F_GETFD)     = %d, errno=%d(%s)  <- 带 CLOEXEC 的 fd,exec 前被内核关了\n",
        fd_cloexec, g, errno, errno == EBADF ? "EBADF" : std::strerror(errno));

    n = ::read(fd_dup, buf, sizeof buf - 1);
    if (n >= 0)
        buf[n] = '\0';
    std::printf("read(%d, ...)          = %ld:\"%s\"  <- 它复制自带 CLOEXEC 的 %d,但 fd 标志不随 "
                "dup 走,又活了\n",
                fd_dup, (long)n, n >= 0 ? buf : "", fd_cloexec);
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 5 && std::strcmp(argv[1], "--child") == 0)
        return child_main(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]));

    banner("a) dup:拿最低空闲 fd;FD_CLOEXEC 不随 dup 复制");
    {
        int fd = sys_call("open", ::open, data_path, O_RDWR | O_CREAT | O_TRUNC, 0666);
        sys_call("write", ::write, fd, "0123456789", 10);
        sys_call("F_SETFD", ::fcntl, fd, F_SETFD, FD_CLOEXEC);
        int cp = sys_call("dup", ::dup, fd);
        std::printf("open 得 fd=%d(F_GETFD=%d),dup 得 fd=%d(F_GETFD=%d)\n", fd,
                    ::fcntl(fd, F_GETFD), cp, ::fcntl(cp, F_GETFD));
        std::printf("原件带着 FD_CLOEXEC,复制件没有——想带过去得再 F_SETFD,或直接用 dup3\n");

        banner("b) 共享偏移:dup 出来的接着读,重新 open 的从头读");
        // fd 偏移在写完 10 字节后是 10,先挪回 0
        sys_call("lseek", ::lseek, fd, 0, SEEK_SET);
        char buf[8];
        ssize_t n1 = sys_call("read", ::read, fd, buf, 4);
        std::printf("fd(%d)  read 4 字节      = \"%.*s\",偏移=%ld\n", fd, (int)n1, buf,
                    (long)sys_call("lseek", ::lseek, fd, 0, SEEK_CUR));
        ssize_t n2 = sys_call("read", ::read, cp, buf, 4);
        std::printf(
            "dup(%d) 接着 read 4 字节 = \"%.*s\",偏移=%ld  <- 同一个打开文件描述,游标共用\n", cp,
            (int)n2, buf, (long)sys_call("lseek", ::lseek, cp, 0, SEEK_CUR));
        int fresh = sys_call("open", ::open, data_path, O_RDONLY);
        ssize_t n3 = sys_call("read", ::read, fresh, buf, 4);
        std::printf("重新 open(%d) read 4 字节 = \"%.*s\"                <- 新描述,游标从头来\n",
                    fresh, (int)n3, buf);
        std::fflush(stdout);
        ::close(fd);
        ::close(cp);
        ::close(fresh);
    }

    banner("c) dup2 原子重定向:printf 落文件;没 fflush 的那条不跟调用时刻走");
    {
        int saved = sys_call("dup", ::dup, 1); // 先留一条回终端的路
        std::fflush(stdout); // 不冲的话,缓冲区里还压着的前文会跟着这次 flush 一起落进文件
        int rf = sys_call("open", ::open, redir_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        int r = sys_call("dup2", ::dup2, rf, 1); // 1 号槽位原子地换给文件
        std::printf("dup2(%d, 1) = %d\n", rf, r);
        std::printf("这条 printf 发生在重定向期间,立刻 fflush → 落进文件\n");
        std::fflush(stdout);
        std::printf("(pending) 这条不 fflush,先躺在 stdio 缓冲区里"); // 注意:不带换行
        sys_call("dup2", ::dup2, saved, 1);                           // 换回终端
        std::printf("  <- dup2 换回 stdout 之后它才被后续输出带着冲出来,落点已是终端\n");
        std::printf("stdio 缓冲区住在用户态:flush 的落点跟着「此刻的 fd 1」走,不跟着 printf "
                    "的调用时刻走\n");
        std::printf("马上验证 redirect.txt 的内容:");
        int chk = sys_call("open", ::open, redir_path, O_RDONLY);
        char buf[256];
        ssize_t n = sys_call("read", ::read, chk, buf, sizeof buf - 1);
        buf[n > 0 ? n : 0] = '\0';
        std::printf("\n---\n%s---\n", buf);
        ::close(chk);
        ::close(rf);
        ::close(saved);
    }

    banner("d) 为什么不 close(1) 再 dup:空窗期 1 号槽会被别的 open 抢走");
    {
        int saved = sys_call("dup", ::dup, 1);
        sys_call("close", ::close, 1); // 1 号槽位空出来
        // 模拟「空窗期里进程里发生的任何一次 open」——多线程程序里这就可能是别的线程干的
        int thief = sys_call("open", ::open, junk_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        // 先把 1 号槽腾回来再恢复,免得 dup2 连抢到的文件一起收走
        sys_call("close", ::close, thief);
        sys_call("dup2", ::dup2, saved, 1); // 收回终端
        std::printf("close(1) 之后,下一次 open 返回的 fd = %d —— 1 号槽被无关 open 抢走了\n",
                    thief);
        std::printf("这时想恢复就得再 close(1)+dup,一来一回全是竞态窗口;dup2 "
                    "一条系统调用收口,这就是「原子」的意义\n");
        ::close(saved);
    }

    banner("e) dup3:带 flags;oldfd==newfd 是 EINVAL,dup2 则是 no-op");
    {
        int fd = sys_call("open", ::open, data_path, O_RDONLY);
        sys_call("F_SETFD", ::fcntl, fd, F_SETFD, FD_CLOEXEC); // 顺便让原件也带上
        int a = sys_call("dup2", ::dup2, fd, 10);
        int b = sys_call("dup3", ::dup3, fd, 11, O_CLOEXEC);
        std::printf("dup2(fd,10)  F_GETFD=%d  dup3(fd,11,O_CLOEXEC) F_GETFD=%d\n",
                    ::fcntl(a, F_GETFD), ::fcntl(b, F_GETFD));
        int r2 = ::dup2(fd, fd);
        std::printf("dup2(fd,fd)  = %d(检查后发现是自己,直接返回,no-op)\n", r2);
        int r3 = ::dup3(fd, fd, 0);
        std::printf("dup3(fd,fd,0)= %d, errno=%d(%s)  <- dup3 明确拒绝\n", r3, errno,
                    errno == EINVAL ? "EINVAL" : std::strerror(errno));
        std::fflush(stdout);
        ::close(a);
        ::close(b);
        ::close(fd);
    }

    banner("f) fork + exec:FD_CLOEXEC 的生死(exec 前先看一眼 fd 表)");
    {
        // 三个角色:
        //   fd_plain  裸 open,不带 CLOEXEC          → 应当活过 exec
        //   fd_clo    open 后用 F_SETFD 补 CLOEXEC   → 应当在 exec 前被内核关闭
        //   fd_dup    从 fd_clo 用 F_DUPFD 复制      → fd 标志不随复制走,应当活过 exec
        int fd_plain = sys_call("open", ::open, data_path, O_RDONLY);
        int fd_clo = sys_call("open", ::open, data_path, O_RDONLY);
        sys_call("F_SETFD", ::fcntl, fd_clo, F_SETFD, FD_CLOEXEC);
        int fd_dup = sys_call("F_DUPFD", ::fcntl, fd_clo, F_DUPFD, 20);

        std::fflush(stdout); // fork 前冲干净,避免缓冲区被父子各打一份
        pid_t pid = sys_call("fork", ::fork);
        if (pid == 0) {
            list_fds("[child exec 前]");
            std::fflush(stdout); // exec 会整个换掉进程映像,缓冲区里没冲的输出会直接丢
            char p[16], c[16], d[16];
            std::snprintf(p, sizeof p, "%d", fd_plain);
            std::snprintf(c, sizeof c, "%d", fd_clo);
            std::snprintf(d, sizeof d, "%d", fd_dup);
            ::execl("/proc/self/exe", "e2_dup(child)", "--child", p, c, d, (char*)nullptr);
            ::perror("execl"); // 只有失败才会走到这
            ::_exit(127);
        }
        int st = 0;
        sys_call("waitpid", ::waitpid, pid, &st, 0);
        std::printf("[parent] 子进程退出码 = %d\n", WEXITSTATUS(st));
        ::close(fd_plain);
        ::close(fd_clo);
        ::close(fd_dup);
    }
    return 0;
}
