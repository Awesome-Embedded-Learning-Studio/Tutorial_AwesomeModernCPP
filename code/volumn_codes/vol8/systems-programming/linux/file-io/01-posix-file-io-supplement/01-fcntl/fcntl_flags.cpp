// fcntl_flags.cpp —— fcntl 两族对照:fd 标志(F_GETFD/F_SETFD)住 fd 表项,
// 文件状态标志(F_GETFL/F_SETFL)住打开文件描述(《POSIX 文件 I/O》补课段 E1)
//
// 观察点(对照 fcntl_flags.out):
//   a) F_GETFD:open 不带 O_CLOEXEC 时出生为 0;F_SETFD(FD_CLOEXEC) 打开;F_SETFD(0) 关上
//   b) F_DUPFD(n):与 dup 等价 + 可指定下限;新 fd 不继承 FD_CLOEXEC;F_DUPFD_CLOEXEC 一步到位
//   c) F_GETFL:拿到「访问模式 + 状态标志」;访问模式必须先用 O_ACCMODE 抠出来再比
//   d) F_SETFL 是整体覆盖不是按位或:不先 F_GETFL 就直接设,原有的 O_APPEND 会被冲掉
//   e) F_SETFL 改不动访问模式:往 O_RDONLY 的 fd 上塞 O_RDWR,调用成功但白塞,write 照样 EBADF
//   f) open 之后想改 O_NONBLOCK 只有 F_SETFL 一条路:重新 open 会得到新的打开文件描述,
//      偏移不共享(这就是「追加写改 O_APPEND 别重开 fd」的原因)
//   g) F_SETFL 加 O_NONBLOCK 后,同一条 read 从阻塞变成立刻 -1/EAGAIN(FIFO 实测)
//   h) 归属总对照:状态标志在描述上,dup/F_DUPFD 出来的 fd 两边都看得到;
//      FD_CLOEXEC 在 fd 表项上,复制出来的 fd 看不到
//
// EINTR 重试这一族(fcntl/read 被信号打断)在 thinking/02 的 02-eintr-retry 已有完整实验,
// 本文不再重复。
#include "article.hpp"

#include <cstdio>
#include <cstring>

#include <sys/stat.h>
#include <sys/types.h>

namespace {

// 数据文件路径烧死:~/l01b_scratch/e1(ext4),复跑前先 mkdir -p
void banner(const char* s) {
    std::printf("\n==== %s ====\n", s);
}

const char* errno_name(int e) {
    switch (e) {
        case EAGAIN:
            return "EAGAIN(Linux 上与 EWOULDBLOCK 同值)";
        case EBADF:
            return "EBADF";
        case EINVAL:
            return "EINVAL";
        default:
            return std::strerror(e);
    }
}

// F_GETFL 的返回值解包:访问模式 + 状态标志,原样八进制也贴出来,不藏着。
// raw 里永远多出一位 0100000(八进制):内核固定带回 __O_LARGEFILE(64 位 off_t 的标记),
// 而 glibc 在 64 位平台把用户态 O_LARGEFILE 定义成 0——所以 F_GETFL 的返回值
// 永远不等于你传给 open 的 flags,比较要按位与,不能判相等
void print_fl(const char* tag, int fl) {
    const int acc = fl & O_ACCMODE;
    std::printf("%-22s raw=0%06o  accmode=%s%s%s%s\n", tag, fl,
                acc == O_RDONLY   ? "O_RDONLY"
                : acc == O_WRONLY ? "O_WRONLY"
                                  : "O_RDWR",
                (fl & O_APPEND) ? " |O_APPEND" : "", (fl & O_NONBLOCK) ? " |O_NONBLOCK" : "",
                (fl & O_ASYNC) ? " |O_ASYNC" : "");
}

// fd 标志的解包:就一位 FD_CLOEXEC
void print_fd(const char* tag, int fdflags) {
    std::printf("%-22s = %d(%s)\n", tag, fdflags,
                fdflags & FD_CLOEXEC ? "FD_CLOEXEC 已设" : "0,无标志");
}

} // namespace

int main() {
    banner("a) F_GETFD / F_SETFD:fd 标志,一位 FD_CLOEXEC,住 fd 表项");
    {
        int fd = sys_call("open", ::open, "/home/charliechen/l01b_scratch/e1/plain.txt",
                          O_RDWR | O_CREAT | O_TRUNC, 0666);
        sys_call("write", ::write, fd, "0123456789", 10);
        print_fd("出生时 F_GETFD", ::fcntl(fd, F_GETFD));
        sys_call("F_SETFD", ::fcntl, fd, F_SETFD, FD_CLOEXEC);
        print_fd("F_SETFD(CLOEXEC) 后", ::fcntl(fd, F_GETFD));
        sys_call("F_SETFD", ::fcntl, fd, F_SETFD, 0);
        print_fd("F_SETFD(0) 清除后", ::fcntl(fd, F_GETFD));

        banner("b) F_DUPFD(n):dup 的等价物,但可以指定下限");
        int d_low = sys_call("dup", ::dup, fd);                   // 永远拿最低空闲
        int d_20 = sys_call("F_DUPFD", ::fcntl, fd, F_DUPFD, 20); // >= 20 的最低空闲
        int d_21 = sys_call("F_DUPFD_CLOEXEC", ::fcntl, fd, F_DUPFD_CLOEXEC, 20);
        std::printf("dup(fd=%d)               = %d   (最低空闲)\n", fd, d_low);
        std::printf("fcntl(fd, F_DUPFD, 20)       = %d   (>=20 的最低空闲)\n", d_20);
        std::printf("fcntl(fd, F_DUPFD_CLOEXEC,20)= %d   (一步到位)\n", d_21);
        std::printf("注意此刻原件已把 FD_CLOEXEC 清掉,三者 F_GETFD 应为 0/0/CLOEXEC:\n");
        print_fd("  d_low F_GETFD", ::fcntl(d_low, F_GETFD));
        print_fd("  d_20  F_GETFD", ::fcntl(d_20, F_GETFD));
        print_fd("  d_21  F_GETFD", ::fcntl(d_21, F_GETFD));
        // 先把三个复制品处理掉,免得干扰后面的 fd 号观察
        ::close(d_low);
        ::close(d_20);
        ::close(d_21);

        banner("c) F_GETFL:访问模式 + 状态标志,O_ACCMODE 抠出访问模式");
        print_fl("F_GETFL(O_RDWR open)", ::fcntl(fd, F_GETFL));
        sys_call("F_SETFL", ::fcntl, fd, F_SETFL, fcntl(fd, F_GETFL) | O_APPEND);
        print_fl("F_SETFL 加 O_APPEND 后", ::fcntl(fd, F_GETFL));

        banner("d) F_SETFL 是整体覆盖:不先 F_GETFL,O_APPEND 会被冲掉");
        sys_call("F_SETFL", ::fcntl, fd, F_SETFL, O_NONBLOCK); // 裸设,不带旧标志
        print_fl("F_SETFL(O_NONBLOCK) 后", ::fcntl(fd, F_GETFL));
        std::printf("O_APPEND 没了——正确姿势是 F_GETFL 读出来、按位或、再 F_SETFL:\n");
        int fl = ::fcntl(fd, F_GETFL);
        sys_call("F_SETFL", ::fcntl, fd, F_SETFL, (fl & ~O_ACCMODE) | O_NONBLOCK | O_APPEND);
        print_fl("补救后", ::fcntl(fd, F_GETFL));

        banner("e) F_SETFL 改不动访问模式");
        int rd = sys_call("open", ::open, "/home/charliechen/l01b_scratch/e1/plain.txt", O_RDONLY);
        print_fl("O_RDONLY open 的 F_GETFL", ::fcntl(rd, F_GETFL));
        int r = ::fcntl(rd, F_SETFL, O_RDWR | O_NONBLOCK); // 想借 F_SETFL 提权写
        std::printf("fcntl(rd, F_SETFL, O_RDWR|O_NONBLOCK) = %d(居然成功)\n", r);
        print_fl("再看 F_GETFL", ::fcntl(rd, F_GETFL));
        ssize_t w = ::write(rd, "x", 1);
        std::printf("write(rd,...) = %ld, errno=%d(%s)  <- 访问模式纹丝不动,白塞\n", (long)w, errno,
                    errno_name(errno));
        ::close(rd);

        banner("f) 改标志别重开:F_SETFL 动原描述保偏移,重新 open 得到新描述");
        // fd 已经写过 10 字节,偏移应为 10
        std::printf("fd   当前偏移 lseek(fd,0,SEEK_CUR)   = %ld\n",
                    (long)sys_call("lseek", ::lseek, fd, 0, SEEK_CUR));
        int fd2 = sys_call("open", ::open, "/home/charliechen/l01b_scratch/e1/plain.txt",
                           O_RDWR | O_APPEND);
        std::printf("fd2  重新 open 的偏移 = %ld  <- 各是各的描述,和 fd 的 10 互不相认\n",
                    (long)sys_call("lseek", ::lseek, fd2, 0, SEEK_CUR));
        std::printf(
            "     (fd2 带着 O_APPEND 出生,lseek 看到的偏移要到 write 那刻才被内核挪到文件尾)\n");
        int fl2 = ::fcntl(fd, F_GETFL);
        sys_call("F_SETFL", ::fcntl, fd, F_SETFL, fl2 & ~O_APPEND); // F_SETFL 摘掉 O_APPEND
        std::printf("fd   F_SETFL 摘掉 O_APPEND 后偏移      = %ld  <- 还是那个描述,偏移没动\n",
                    (long)sys_call("lseek", ::lseek, fd, 0, SEEK_CUR));
        ::close(fd2);
        ::close(fd);
    }

    banner("g) F_SETFL 加 O_NONBLOCK:同一条 read 从阻塞变 -1/EAGAIN(FIFO)");
    {
        const char* fifo = "/home/charliechen/l01b_scratch/e1/nonblock.fifo";
        ::unlink(fifo); // 复跑友好
        if (::mkfifo(fifo, 0600) == -1) {
            std::perror("mkfifo");
            return 1;
        }
        // O_RDWR 自带写端:open 不用等对端,read 空管道时也不会 EOF,阻塞态可以复现
        int f = sys_call("open", ::open, fifo, O_RDWR);
        print_fl("出生(阻塞)", ::fcntl(f, F_GETFL));
        std::printf("此时 read 会永远阻塞,咱们不真调;F_SETFL 加 O_NONBLOCK 再看:\n");
        sys_call("F_SETFL", ::fcntl, f, F_SETFL, ::fcntl(f, F_GETFL) | O_NONBLOCK);
        print_fl("F_SETFL 后", ::fcntl(f, F_GETFL));
        char buf[16];
        ssize_t n = ::read(f, buf, sizeof buf);
        std::printf("read(f,...) = %ld, errno=%d = %s  <- 没数据就立刻返回,不再等\n", (long)n,
                    errno, errno_name(errno));

        banner("h) 归属总对照:同一个 fd 复制件,两族标志两种待遇");
        int g = sys_call("F_DUPFD", ::fcntl, f, F_DUPFD, 0); // 与 dup(f) 等价
        // f 上补一个 FD_CLOEXEC,方便对照
        sys_call("F_SETFD", ::fcntl, f, F_SETFD, FD_CLOEXEC);
        std::printf("            f(原件)   g(F_DUPFD 复制件)\n");
        std::printf("O_NONBLOCK  %-8s %-8s <- 状态标志在描述上,两个 fd 共享\n",
                    ::fcntl(f, F_GETFL) & O_NONBLOCK ? "有" : "无",
                    ::fcntl(g, F_GETFL) & O_NONBLOCK ? "有" : "无");
        std::printf("FD_CLOEXEC  %-8s %-8s <- fd 标志在表项上,复制件不带走\n",
                    ::fcntl(f, F_GETFD) & FD_CLOEXEC ? "有" : "无",
                    ::fcntl(g, F_GETFD) & FD_CLOEXEC ? "有" : "无");
        ::close(g);
        ::close(f);
        ::unlink(fifo);
    }
    return 0;
}
