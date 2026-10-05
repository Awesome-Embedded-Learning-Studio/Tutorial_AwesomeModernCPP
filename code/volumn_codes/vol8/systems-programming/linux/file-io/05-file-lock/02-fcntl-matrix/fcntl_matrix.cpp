// e2_fcntl_matrix.cpp —— fcntl 记录锁语义矩阵(文章《文件锁:flock 与 fcntl 记录锁》E2)
//
// 六组观察(时间戳 = 相对程序启动的毫秒数):
//   a) 字节区间锁:A 锁 [0,100),B 试 [50,150) 冲突 / [100,200) 成功
//   b) F_GETLK 探测冲突:返回对方 pid 与对方锁的实际区间
//   c) 读锁共享 / 写锁排他:R+R 共存,凡带 W 即冲突
//   d) 【全篇最大陷阱】同进程 close 该文件的任意一个 fd,
//      释放该进程在此文件上的全部记录锁;OFD 锁(F_OFD_SETLK)没有这个陷阱
//   e) 区间重叠的替换语义:后一把锁覆盖重叠段,且同进程第二把锁永不自冲突
//   f) 进程退出释放(持锁子进程直接 _exit,竞争者立刻拿得到)
//
// 同步:持锁者与竞争者用管道握手,不靠 sleep 赌时序。
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE // F_OFD_SETLK / F_OFD_GETLK 在 glibc 的 __USE_GNU 段
#endif

#include "article.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/wait.h>
#include <unistd.h>

namespace {

const char* path = "/home/charliechen/l05_scratch/e2/fcntl.bin";

const auto t0 = std::chrono::steady_clock::now();

long ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 t0)
        .count();
}

void banner(const char* s) {
    std::printf("\n==== %s ====\n", s);
}

const char* ltype_name(short t) {
    switch (t) {
        case F_RDLCK:
            return "F_RDLCK";
        case F_WRLCK:
            return "F_WRLCK";
        case F_UNLCK:
            return "F_UNLCK";
        default:
            return "?";
    }
}

struct lock_spec {
    short type;
    off_t start;
    off_t len; // 0 = 到 EOF

    ::flock fl() const {
        ::flock f{};
        f.l_type = type;
        f.l_whence = SEEK_SET;
        f.l_start = start;
        f.l_len = len;
        f.l_pid = 0;
        return f;
    }
};

void prep_file() {
    unique_fd fd{sys_call("open", ::open, path, O_RDWR | O_CREAT | O_TRUNC, 0666)};
}

// ---------------------------------------------------------------------------
// 持锁者:子进程。上锁 → 通过 st 管道报 "L" → 阻塞等 cmd 管道:
//   'x' = 直接 _exit(用进程退出释放锁)   'u' = 先 F_UNLCK 再退
// ---------------------------------------------------------------------------
struct holder {
    pid_t pid;
    int cmd_fd; // 父进程写
    int st_fd;  // 父进程读
};

holder start_holder(lock_spec s) {
    int cmd_pipe[2], st_pipe[2];
    sys_call("pipe", ::pipe, cmd_pipe);
    sys_call("pipe", ::pipe, st_pipe);
    pid_t pid = ::fork();
    if (pid == 0) {
        ::close(cmd_pipe[1]);
        ::close(st_pipe[0]);
        unique_fd fd{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
        ::flock f = s.fl();
        if (::fcntl(fd.get(), F_SETLK, &f) == -1) {
            std::printf("  [%6ld ms] 持锁者(pid=%d):F_SETLK 失败 errno=%d\n", ms(), ::getpid(),
                        errno);
            ::_exit(1);
        }
        char c = 'L';
        sys_call("write", ::write, st_pipe[1], &c, 1);
        char cmd = 0;
        if (::read(cmd_pipe[0], &cmd, 1) == 1 && cmd == 'u') {
            ::flock u{};
            u.l_type = F_UNLCK;
            u.l_whence = SEEK_SET;
            u.l_start = 0;
            u.l_len = 0; // 0 = 整个文件
            ::fcntl(fd.get(), F_SETLK, &u);
        }
        ::_exit(0);
    }
    ::close(cmd_pipe[0]);
    ::close(st_pipe[1]);
    char c = 0;
    if (::read(st_pipe[0], &c, 1) != 1 || c != 'L') {
        std::printf("  !! 持锁者 %d 没有报就位\n", pid);
    }
    return holder{pid, cmd_pipe[1], st_pipe[0]};
}

void stop_holder(holder& h, char cmd) {
    sys_call("write", ::write, h.cmd_fd, &cmd, 1);
    int st = 0;
    ::waitpid(h.pid, &st, 0);
    ::close(h.cmd_fd);
    ::close(h.st_fd);
}

// ---------------------------------------------------------------------------
// 竞争者/探针:子进程,全新 open。三种打法:
//   SETLK    —— F_SETLK 试锁,汇报返回值与 errno
//   GETLK    —— F_GETLK 探测,汇报找到的冲突锁(类型/pid/区间)
//   OFD_GETLK —— F_OFD_GETLK 探测(对 OFD 锁能给出 pid 吗?)
// ---------------------------------------------------------------------------
enum class op { SETLK, GETLK, OFD_GETLK };

pid_t spawn_probe(op o, lock_spec s, const char* tag) {
    pid_t pid = ::fork();
    if (pid == 0) {
        unique_fd own{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
        ::flock f = s.fl();
        if (o == op::SETLK) {
            int r = ::fcntl(own.get(), F_SETLK, &f);
            std::printf("  [%6ld ms] %s(pid=%d):F_SETLK %s [%ld,%ld) = %d", ms(), tag, ::getpid(),
                        ltype_name(s.type), static_cast<long>(s.start),
                        s.len ? static_cast<long>(s.start + s.len) : -1L, r);
            if (r == -1) {
                std::printf(", errno=%d(%s)", errno,
                            errno == EAGAIN ? "EAGAIN(即 EWOULDBLOCK)" : std::strerror(errno));
            }
            std::printf("\n");
        } else {
            int cmd = (o == op::GETLK) ? F_GETLK : F_OFD_GETLK;
            int r = ::fcntl(own.get(), cmd, &f);
            if (f.l_type == F_UNLCK) {
                std::printf("  [%6ld ms] %s(pid=%d):%s %s [%ld,%ld) → 无冲突(l_type=F_UNLCK)\n",
                            ms(), tag, ::getpid(), o == op::GETLK ? "F_GETLK" : "F_OFD_GETLK",
                            ltype_name(s.type), static_cast<long>(s.start),
                            s.len ? static_cast<long>(s.start + s.len) : -1L);
            } else {
                std::printf("  [%6ld ms] %s(pid=%d):%s %s [%ld,%ld) → 撞上 %s l_pid=%d, "
                            "区间[%ld,%ld)(r=%d)\n",
                            ms(), tag, ::getpid(), o == op::GETLK ? "F_GETLK" : "F_OFD_GETLK",
                            ltype_name(s.type), static_cast<long>(s.start),
                            s.len ? static_cast<long>(s.start + s.len) : -1L, ltype_name(f.l_type),
                            f.l_pid, static_cast<long>(f.l_start),
                            f.l_len ? static_cast<long>(f.l_start + f.l_len) : -1L, r);
            }
        }
        ::_exit(0);
    }
    int st = 0;
    ::waitpid(pid, &st, 0);
    return pid;
}

// ---------------------------------------------------------------------------
void case_a() {
    banner("E2a  字节区间:A 锁 [0,100),B 试 [50,150) 冲突、[100,200) 成功");
    holder h = start_holder({F_WRLCK, 0, 100});
    std::printf("  [%6ld ms] A(持锁者 pid=%d):F_SETLK F_WRLCK [0,100) = 0\n", ms(), h.pid);
    spawn_probe(op::SETLK, {F_WRLCK, 50, 100}, "B");
    spawn_probe(op::SETLK, {F_WRLCK, 100, 100}, "B");
    stop_holder(h, 'x');
    std::printf("  [%6ld ms] A 已 _exit(0)(见 E2f:进程退出即释放)\n", ms());
    spawn_probe(op::SETLK, {F_WRLCK, 0, 50}, "B");
}

void case_b() {
    banner("E2b  F_GETLK:探测冲突锁的 pid 与实际区间");
    holder h = start_holder({F_WRLCK, 0, 100});
    std::printf("  [%6ld ms] A(持锁者 pid=%d) 持 F_WRLCK [0,100)\n", ms(), h.pid);
    spawn_probe(op::GETLK, {F_WRLCK, 0, 200}, "探针B");  // 全覆盖
    spawn_probe(op::GETLK, {F_WRLCK, 150, 50}, "探针B"); // 无交集
    spawn_probe(op::GETLK, {F_WRLCK, 50, 10}, "探针B");  // 部分交集
    stop_holder(h, 'u');
}

void case_c() {
    banner("E2c  读共享/写排他:四种组合");
    struct combo {
        short a, b;
    };
    const combo cs[] = {
        {F_RDLCK, F_RDLCK}, {F_RDLCK, F_WRLCK}, {F_WRLCK, F_RDLCK}, {F_WRLCK, F_WRLCK}};
    for (auto c : cs) {
        holder h = start_holder({c.a, 0, 100});
        std::printf("  A 持 %s [0,100) 时,B 试 %s [0,100):", ltype_name(c.a), ltype_name(c.b));
        spawn_probe(op::SETLK, {c.b, 0, 100}, "B");
        stop_holder(h, 'x');
    }
}

void case_d() {
    banner("E2d  陷阱:同进程 close 任意一个 fd → 全部记录锁释放");
    // 变体一:锁了之后才 open 的第二个 fd
    unique_fd fd1{sys_call("open", ::open, path, O_RDWR)};
    ::flock f = lock_spec{F_WRLCK, 0, 100}.fl();
    int r = ::fcntl(fd1.get(), F_SETLK, &f);
    std::printf("  [%6ld ms] 本进程(pid=%d):fd1 上 F_SETLK W [0,100) = %d\n", ms(), ::getpid(), r);
    spawn_probe(op::SETLK, {F_WRLCK, 0, 100}, "竞争者");
    {
        unique_fd fd2{sys_call("open", ::open, path, O_RDONLY)}; // 锁后 open
        std::printf("  [%6ld ms] 本进程:open 第二个 fd(只读都行),随即 close(fd2)\n", ms());
    }
    std::printf("  [%6ld ms] 本进程:fd2 已 close,fd1 还开着,一次 LOCK_UN 都没调\n", ms());
    spawn_probe(op::SETLK, {F_WRLCK, 0, 100}, "竞争者");

    // 变体二:锁之前就 open 好的 fd,close 它同样全释放
    unique_fd fd0{sys_call("open", ::open, path, O_RDWR)};
    unique_fd fd1b{sys_call("open", ::open, path, O_RDWR)};
    r = ::fcntl(fd1b.get(), F_SETLK, &f);
    std::printf("  [%6ld ms] 变体二:本进程经 fd1b 再锁 [0,100) = %d(fd0 早开着)\n", ms(), r);
    spawn_probe(op::SETLK, {F_WRLCK, 0, 100}, "竞争者");
    fd0.reset(); // close 锁之前就 open 的 fd0
    std::printf("  [%6ld ms] 本进程:close(fd0) —— 关的不是加锁那个 fd\n", ms());
    spawn_probe(op::SETLK, {F_WRLCK, 0, 100}, "竞争者");

#ifdef F_OFD_SETLK
    banner("E2d'  对照:F_OFD_SETLK(锁挂在打开文件描述上)没有这个陷阱");
    unique_fd ofd1{sys_call("open", ::open, path, O_RDWR)};
    r = ::fcntl(ofd1.get(), F_OFD_SETLK, &f);
    std::printf("  [%6ld ms] 本进程:fd1 上 F_OFD_SETLK W [0,100) = %d\n", ms(), r);
    spawn_probe(op::GETLK, {F_WRLCK, 0, 100}, "探针"); // l_pid 应为 -1
    spawn_probe(op::OFD_GETLK, {F_WRLCK, 0, 100}, "OFD探针");
    spawn_probe(op::SETLK, {F_WRLCK, 0, 100}, "竞争者");
    {
        unique_fd ofd2{sys_call("open", ::open, path, O_RDONLY)};
        std::printf("  [%6ld ms] 本进程:open 第二个 fd 并 close —— 锁不受影响?\n", ms());
    }
    spawn_probe(op::SETLK, {F_WRLCK, 0, 100}, "竞争者");
    ofd1.reset(); // 关掉真正持锁的 fd 才释放
    std::printf("  [%6ld ms] 本进程:close(持锁的 fd1)\n", ms());
    spawn_probe(op::SETLK, {F_WRLCK, 0, 100}, "竞争者");
#endif
}

void case_e() {
    banner("E2e  替换语义:后锁覆盖重叠段;同进程第二把锁不自冲突");
    unique_fd fd1{sys_call("open", ::open, path, O_RDWR)};
    unique_fd fd2{sys_call("open", ::open, path, O_RDWR)}; // 同进程第二个 fd
    ::flock f = lock_spec{F_WRLCK, 0, 100}.fl();
    sys_call("F_SETLK W[0,100)", ::fcntl, fd1.get(), F_SETLK, &f);
    std::printf("  [%6ld ms] 本进程(pid=%d):fd1 上 W [0,100)\n", ms(), ::getpid());
    ::flock f2 = lock_spec{F_WRLCK, 0, 100}.fl();
    int r = ::fcntl(fd2.get(), F_SETLK, &f2);
    std::printf("  [%6ld ms] 本进程:fd2 上再 W [0,100) = %d(POSIX 锁按进程算,永不自冲突)\n", ms(),
                r);

    // 在同一进程里把 [50,150) 改成读锁 → 重叠段 [50,100) 被替换成 R
    ::flock fr = lock_spec{F_RDLCK, 50, 100}.fl();
    r = ::fcntl(fd1.get(), F_SETLK, &fr);
    std::printf("  [%6ld ms] 本进程:fd1 上 R [50,150) = %d(重叠段被替换)\n", ms(), r);

    // 探针(必须是别的进程):读探针只撞写锁,写探针读写都撞
    spawn_probe(op::GETLK, {F_RDLCK, 0, 50}, "读探针");   // 期望撞 W
    spawn_probe(op::GETLK, {F_RDLCK, 50, 100}, "读探针"); // 期望无冲突
    spawn_probe(op::GETLK, {F_WRLCK, 50, 100}, "写探针"); // 期望撞 R

    ::flock u{};
    u.l_type = F_UNLCK;
    u.l_whence = SEEK_SET;
    u.l_start = 0;
    u.l_len = 0;
    ::fcntl(fd1.get(), F_SETLK, &u);
    spawn_probe(op::SETLK, {F_WRLCK, 0, 200}, "竞争者"); // 确认清场
}

void case_f() {
    banner("E2f  进程退出释放:持锁者 _exit,竞争者立刻拿得到");
    holder h = start_holder({F_WRLCK, 0, 100});
    std::printf("  [%6ld ms] A(持锁者 pid=%d):持 W [0,100)\n", ms(), h.pid);
    spawn_probe(op::SETLK, {F_WRLCK, 0, 100}, "竞争者");
    stop_holder(h, 'x'); // 'x' = 直接 _exit,不 unlock 不 close
    std::printf("  [%6ld ms] A 已 _exit(0),没有任何放锁动作\n", ms());
    spawn_probe(op::SETLK, {F_WRLCK, 0, 100}, "竞争者");
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    prep_file();
    std::printf("pid=%d, 数据文件:%s(ext4)\n", ::getpid(), path);
    case_a();
    case_b();
    case_c();
    case_d();
    case_e();
    case_f();
    return 0;
}
