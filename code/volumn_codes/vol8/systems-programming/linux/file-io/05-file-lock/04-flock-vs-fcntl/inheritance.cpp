// e4_inheritance.cpp —— flock vs fcntl 继承行为对照(文章《文件锁》E4)
//
// 同一个二进制,argv[1] 选场景;主场景里再 fork 竞争者做验证:
//   fork-flock   : flock 锁属于打开文件描述 → fork 继承的 fd 与父共享同一把锁,
//                  子进程可直接 flock、还能替父 LOCK_UN;子 close 继承 fd 不放锁
//   fork-fcntl   : POSIX 记录锁属于进程 → fork 不继承;子进程(哪怕用继承的 fd)
//                  加锁撞父进程的锁,F_GETLK 报出父 pid
//   dup-flock    : dup 与 fork 同源(复制描述引用) → 锁跟随描述;close 全部副本才释放;
//                  对照:重新 open 的新描述会与旧描述自冲突(E1b 已证,这里补 close 链)
//   exec-flock   : fd 不带 O_CLOEXEC → flock 锁随打开文件描述活过 exec;
//                  带 O_CLOEXEC → fd 在 exec 瞬间关闭,锁当场释放
//   exec-fcntl   : POSIX 记录锁属于进程 → exec 不影响(锁跟着进程走,不跟 fd)
//
// 竞争者一律「自己 open 新描述」,避免又踩进继承描述的坑。
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif

#include "article.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>

#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

const char* path = "/home/charliechen/l05_scratch/e4/inherit.bin";

const auto t0 = std::chrono::steady_clock::now();

long ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 t0)
        .count();
}

void banner(const char* s) {
    std::printf("\n==== %s ====\n", s);
}

// 竞争者:自己 open,flock 非阻塞试锁,汇报后退场
void contender_flock(const char* tag) {
    pid_t pid = ::fork();
    if (pid == 0) {
        unique_fd own{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
        int r = ::flock(own.get(), LOCK_EX | LOCK_NB);
        std::printf("  [%6ld ms] 竞争者(%s):flock(LOCK_EX|LOCK_NB) = %d%s\n", ms(), tag, r,
                    r == -1 ? "(锁被占)" : "(拿到锁)");
        ::_exit(0);
    }
    int st = 0;
    ::waitpid(pid, &st, 0);
}

// 竞争者:自己 open,F_SETLK 试写锁 [0,100),汇报后退场
void contender_fcntl(const char* tag) {
    pid_t pid = ::fork();
    if (pid == 0) {
        unique_fd own{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
        struct ::flock f{};
        f.l_type = F_WRLCK;
        f.l_whence = SEEK_SET;
        f.l_start = 0;
        f.l_len = 100;
        int r = ::fcntl(own.get(), F_SETLK, &f);
        std::printf("  [%6ld ms] 竞争者(%s):F_SETLK W[0,100) = %d%s\n", ms(), tag, r,
                    r == -1 ? "(锁被占)" : "(拿到锁)");
        ::_exit(0);
    }
    int st = 0;
    ::waitpid(pid, &st, 0);
}

void run_fork_flock() {
    banner("fork × flock:锁跟着打开文件描述走");
    unique_fd fd{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
    sys_call("flock", ::flock, fd.get(), LOCK_EX);
    std::printf("  [%6ld ms] 父(pid=%d):持 flock\n", ms(), ::getpid());

    pid_t k = ::fork();
    if (k == 0) {                           // 子进程用继承的 fd:同描述,不冲突,还能替父放锁
        int r = ::flock(fd.get(), LOCK_EX); // 阻塞版也直接返回
        std::printf("  [%6ld ms] 子(pid=%d):继承 fd 上 flock(LOCK_EX) = %d\n", ms(), ::getpid(), r);
        int r2 = ::flock(fd.get(), LOCK_UN);
        std::printf("  [%6ld ms] 子:LOCK_UN = %d —— 放的是父进程的锁\n", ms(), r2);
        ::_exit(0);
    }
    int st = 0;
    ::waitpid(k, &st, 0);
    contender_flock("子 UN 后"); // 预期拿到:子已替父放锁

    sys_call("flock", ::flock, fd.get(), LOCK_EX);
    std::printf("  [%6ld ms] 父:再持 flock,fork 出只 close 继承 fd 的子进程\n", ms());
    pid_t k2 = ::fork();
    if (k2 == 0) {
        ::close(fd.get());
        std::printf("  [%6ld ms] 子(pid=%d):close(继承 fd)\n", ms(), ::getpid());
        ::_exit(0);
    }
    ::waitpid(k2, &st, 0);
    contender_flock("子 close 后"); // 预期被占:父的 fd 还引用着同一描述
}

void run_fork_fcntl() {
    banner("fork × fcntl:记录锁跟着进程走,fork 不继承");
    unique_fd fd{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
    struct ::flock f{};
    f.l_type = F_WRLCK;
    f.l_whence = SEEK_SET;
    f.l_start = 0;
    f.l_len = 100;
    sys_call("F_SETLK", ::fcntl, fd.get(), F_SETLK, &f);
    std::printf("  [%6ld ms] 父(pid=%d):持 F_SETLK W[0,100)\n", ms(), ::getpid());

    pid_t k = ::fork();
    if (k == 0) { // 子进程:继承的 fd 上加锁 → 撞的是父进程的锁
        int r = ::fcntl(fd.get(), F_SETLK, &f);
        std::printf("  [%6ld ms] 子(pid=%d):继承 fd 上 F_SETLK W[0,100) = %d, errno=%d\n", ms(),
                    ::getpid(), r, r == -1 ? errno : 0);
        // F_GETLK 看看是谁挡的
        struct ::flock probe{};
        probe.l_type = F_WRLCK;
        probe.l_whence = SEEK_SET;
        probe.l_start = 0;
        probe.l_len = 100;
        ::fcntl(fd.get(), F_GETLK, &probe);
        std::printf("  [%6ld ms] 子:F_GETLK → l_pid=%d(%s),锁不随 fork 继承\n", ms(), probe.l_pid,
                    probe.l_pid == ::getppid() ? "=父pid" : "?");
        ::_exit(0);
    }
    int st = 0;
    ::waitpid(k, &st, 0);
    contender_fcntl("对照"); // 独立进程照样被挡
}

void run_dup_flock() {
    banner("dup × flock:close 的引用计数语义");
    unique_fd fd1{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
    int fd2 = ::dup(fd1.get());
    sys_call("flock", ::flock, fd1.get(), LOCK_EX);
    std::printf("  [%6ld ms] fd1 持锁,dup 出 fd2(同一描述的两个引用)\n", ms());
    fd1.reset(); // close(fd1):描述还有 fd2 引用 → 锁不放
    std::printf("  [%6ld ms] close(fd1)\n", ms());
    contender_flock("close fd1 后"); // 预期被占
    ::close(fd2);                    // 最后一个引用关闭 → 锁释放
    std::printf("  [%6ld ms] close(fd2)(最后一个引用)\n", ms());
    contender_flock("close fd2 后"); // 预期拿到
}

// exec 助手:重新执行自己,带一个模式参数
void reexec(const char* mode) {
    char self[256];
    ssize_t n = ::readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) {
        std::fprintf(stderr, "readlink 失败\n");
        ::_exit(126);
    }
    self[n] = '\0';
    char* av[] = {self, const_cast<char*>(mode), nullptr};
    ::execv(self, av);
}

void run_exec_flock() {
    banner("exec × flock:锁挂描述,fd 不关就活过 exec");
    pid_t k = ::fork();
    if (k == 0) { // 子进程:自己 open + 持锁,然后 exec 自己 —— fd 是它唯一的锁柄
        unique_fd fd{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
        sys_call("flock", ::flock, fd.get(), LOCK_EX);
        std::printf("  [%6ld ms] 子(pid=%d):open + flock 后 exec 自己(无 O_CLOEXEC)\n", ms(),
                    ::getpid());
        fd.release(); // 所有权交给 exec 后的新映像,别让析构 close
        reexec("exec-child-flock");
        ::_exit(127);
    }
    ::usleep(300 * 1000);
    contender_flock("exec 后窗口内"); // 预期被占:锁活过了 exec
    int st = 0;
    ::waitpid(k, &st, 0);
    contender_flock("exec 子退场后"); // 预期拿到:最后一个 fd 引用关闭
}

void run_exec_cloexec() {
    banner("exec × flock × O_CLOEXEC:fd 在 exec 瞬间关闭,锁当场释放");
    pid_t k = ::fork();
    if (k == 0) {
        unique_fd fd{sys_call("open", ::open, path, O_RDWR | O_CREAT | O_CLOEXEC, 0666)};
        sys_call("flock", ::flock, fd.get(), LOCK_EX);
        std::printf("  [%6ld ms] 子(pid=%d):flock(O_CLOEXEC fd) 后 exec 自己\n", ms(), ::getpid());
        fd.release();
        reexec("exec-child-cloexec"); // exec 瞬间 fd 关闭 → 锁当场释放
        ::_exit(127);
    }
    ::usleep(300 * 1000);
    contender_flock("exec 后窗口内"); // 预期拿到:锁已随 fd 关闭而释放
    int st = 0;
    ::waitpid(k, &st, 0);
}

void run_exec_fcntl() {
    banner("exec × fcntl:锁挂进程,exec 不动它");
    pid_t k = ::fork();
    if (k == 0) { // 必须由「上锁的这个进程」亲自 exec,才谈得上锁活过 exec
        unique_fd fd{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
        struct ::flock f{};
        f.l_type = F_WRLCK;
        f.l_whence = SEEK_SET;
        f.l_start = 0;
        f.l_len = 100;
        sys_call("F_SETLK", ::fcntl, fd.get(), F_SETLK, &f);
        std::printf("  [%6ld ms] 子(pid=%d):F_SETLK W[0,100) 后 exec 自己\n", ms(), ::getpid());
        fd.release();
        reexec("exec-child-fcntl"); // exec 后锁仍属于同一进程
        ::_exit(127);
    }
    ::usleep(300 * 1000);
    contender_fcntl("exec 后窗口内"); // 预期被占:锁活过了 exec
    int st = 0;
    ::waitpid(k, &st, 0);
    contender_fcntl("exec 子退场后"); // 预期拿到:进程退出释放
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc > 1) {
        if (std::strcmp(argv[1], "exec-child-flock") == 0) {
            std::printf("  [%6ld ms] exec 后的进程(pid=%d):fd 还握着,锁还在,600 ms 后退场\n", ms(),
                        ::getpid());
            ::usleep(600 * 1000);
            return 0;
        }
        if (std::strcmp(argv[1], "exec-child-cloexec") == 0) {
            std::printf("  [%6ld ms] exec 后的进程(pid=%d):fd 已被 O_CLOEXEC 关掉\n", ms(),
                        ::getpid());
            ::usleep(600 * 1000); // 不持任何锁,纯对齐节奏
            return 0;
        }
        if (std::strcmp(argv[1], "exec-child-fcntl") == 0) {
            std::printf("  [%6ld ms] exec 后的进程(pid=%d):fcntl 锁仍属于本进程,600 ms 后退场\n",
                        ms(), ::getpid());
            ::usleep(600 * 1000);
            return 0;
        }
    }

    { // 清场
        unique_fd fd{sys_call("open", ::open, path, O_RDWR | O_CREAT | O_TRUNC, 0666)};
    }
    std::printf("pid=%d, 文件:%s(ext4)\n", ::getpid(), path);
    run_fork_flock();
    run_fork_fcntl();
    run_dup_flock();
    run_exec_flock();
    run_exec_cloexec();
    run_exec_fcntl();
    return 0;
}
