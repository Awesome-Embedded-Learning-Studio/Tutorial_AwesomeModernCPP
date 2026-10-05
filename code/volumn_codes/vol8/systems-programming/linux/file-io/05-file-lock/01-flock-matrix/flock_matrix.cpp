// e1_flock_matrix.cpp —— flock 语义矩阵(文章《文件锁:flock 与 fcntl 记录锁》E1)
//
// 五组观察,全部两进程(父子 fork)或同进程实测,时间戳是相对程序启动的毫秒数:
//   a) LOCK_EX 互斥:第二个进程阻塞,拿到锁的时序贴着第一个进程的解锁瞬间
//   b) 同一进程两次 flock:同一 fd(转换,立即成功)/ dup(同一打开文件描述,立即成功)/
//      重新 open(新描述,自冲突;阻塞版自锁死,用 alarm 收尸取证)
//   c) LOCK_NB 非阻塞:冲突时返回 -1,errno=EWOULDBLOCK
//   d) close(fd) 隐式释放:持有者 close,竞争者立刻拿得到
//   e) fork 继承:子进程继承的是同一个「打开文件描述」——锁跟着描述走,
//      子进程 close 继承的 fd 不释放锁(父进程手里的 fd 还引用着同一描述)
#include "article.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

const char* path = "/home/charliechen/l05_scratch/e1/flock.bin";

const auto t0 = std::chrono::steady_clock::now();

long ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 t0)
        .count();
}

const char* errno_name(int e) {
    return e == EWOULDBLOCK ? "EWOULDBLOCK" : std::strerror(e);
}

void banner(const char* s) {
    std::printf("\n==== %s ====\n", s);
}

// 起一个「竞争者」子进程:自己 open(全新打开文件描述),非阻塞试锁,汇报后退场
pid_t spawn_contender() {
    pid_t pid = ::fork();
    if (pid == 0) {
        unique_fd own{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
        int r = ::flock(own.get(), LOCK_EX | LOCK_NB);
        std::printf("  [%6ld ms] 竞争者(pid=%d, 自己open):flock(LOCK_EX|LOCK_NB) = %d", ms(),
                    ::getpid(), r);
        if (r == -1) {
            std::printf(", errno=%d(%s)", errno, errno_name(errno));
        }
        std::printf("\n");
        ::_exit(0);
    }
    return pid;
}

void reap(pid_t pid) {
    int st = 0;
    if (::waitpid(pid, &st, 0) == -1 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        std::printf("  !! 子进程 %d 异常退场 st=%#x\n", pid, st);
    }
}

void prep_file() {
    unique_fd fd{sys_call("open", ::open, path, O_RDWR | O_CREAT | O_TRUNC, 0666)};
}

// ---------------------------------------------------------------------------
// a) LOCK_EX 互斥 + 拿锁时序
// ---------------------------------------------------------------------------
void case_a() {
    banner("E1a  LOCK_EX 互斥:第二进程阻塞,解锁瞬间接棒");
    // fork 出来的子进程继承同一打开文件描述,不会与父进程的锁冲突(见 E1e),
    // 所以竞争者必须自己 open —— 拿到的是新描述,才看得到互斥。
    unique_fd fd{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
    sys_call("flock", ::flock, fd.get(), LOCK_EX);
    std::printf("  [%6ld ms] A(pid=%d):flock(LOCK_EX) = 0,持锁\n", ms(), ::getpid());

    pid_t c = ::fork();
    if (c == 0) { // C:自己 open + 阻塞版
        unique_fd own{sys_call("open", ::open, path, O_RDWR)};
        sys_call("flock", ::flock, own.get(), LOCK_EX);
        std::printf("  [%6ld ms] C(pid=%d):自己 open + 阻塞 flock(LOCK_EX) 返回 0,拿到锁\n", ms(),
                    ::getpid());
        ::_exit(0);
    }
    ::usleep(400 * 1000); // 持锁 400 ms
    sys_call("flock", ::flock, fd.get(), LOCK_UN);
    std::printf("  [%6ld ms] A:LOCK_UN,放锁 → C 的拿锁时刻应紧贴这一行\n", ms());
    reap(c);
}

// ---------------------------------------------------------------------------
// b) 同一进程两次 flock
// ---------------------------------------------------------------------------
void case_b() {
    banner("E1b  同一进程两次 flock:同 fd / dup / 重新 open");
    unique_fd fd1{sys_call("open", ::open, path, O_RDWR)};

    sys_call("flock", ::flock, fd1.get(), LOCK_EX);
    std::printf("  [%6ld ms] fd1: flock(LOCK_EX) = 0\n", ms());
    int r = ::flock(fd1.get(), LOCK_EX);
    std::printf("  [%6ld ms] fd1 再 flock(LOCK_EX) = %d(同一描述,重复加锁即转换)\n", ms(), r);
    r = ::flock(fd1.get(), LOCK_SH);
    std::printf("  [%6ld ms] fd1 再 flock(LOCK_SH) = %d(同一描述上 EX→SH 转换)\n", ms(), r);
    sys_call("flock", ::flock, fd1.get(), LOCK_UN);

    int dup_fd = ::dup(fd1.get());
    sys_call("flock", ::flock, fd1.get(), LOCK_EX);
    r = ::flock(dup_fd, LOCK_EX | LOCK_NB);
    std::printf("  [%6ld ms] dup(fd1) 上 flock(LOCK_EX|LOCK_NB) = %d(dup 共享同一描述,不冲突)\n",
                ms(), r);
    sys_call("flock", ::flock, fd1.get(), LOCK_UN);
    ::close(dup_fd);

    unique_fd fd2{sys_call("open", ::open, path, O_RDWR)}; // 重新 open = 新描述
    sys_call("flock", ::flock, fd1.get(), LOCK_EX);
    reap(spawn_contender()); // 独立进程口径的对照
    int r2 = ::flock(fd2.get(), LOCK_EX | LOCK_NB);
    std::printf("  [%6ld ms] 重新 open 的 fd2:flock(LOCK_EX|LOCK_NB) = %d, errno=%d(%s)\n", ms(),
                r2, errno, errno_name(errno));
    std::printf("  [%6ld ms]            → 同进程两个描述也会自冲突\n", ms());

    // 阻塞版自锁死取证:子进程对 fd2 阻塞 flock,2 秒 alarm 到点杀掉
    pid_t d = ::fork();
    if (d == 0) {
        ::signal(SIGALRM, SIG_DFL);
        ::alarm(2);
        ::flock(fd2.get(), LOCK_EX); // 预期永远不返回
        ::_exit(0);                  // 若真返回了,以 0 退场戳穿预期
    }
    std::printf("  [%6ld ms] 子进程对 fd2 用阻塞版 flock,2 s alarm 护航…\n", ms());
    int st = 0;
    ::waitpid(d, &st, 0);
    if (WIFSIGNALED(st) && WTERMSIG(st) == SIGALRM) {
        std::printf("  [%6ld ms] 子进程被 SIGALRM 击杀:阻塞版 flock 2 s 未返回 → 自锁死\n", ms());
    } else {
        std::printf("  [%6ld ms] !! 阻塞版居然返回了,st=%#x\n", ms(), st);
    }
    sys_call("flock", ::flock, fd1.get(), LOCK_UN);
}

// ---------------------------------------------------------------------------
// c) LOCK_NB 冲突返回 EWOULDBLOCK(两进程口径)
// ---------------------------------------------------------------------------
void case_c() {
    banner("E1c  LOCK_NB:冲突时 -1 + EWOULDBLOCK");
    unique_fd fd{sys_call("open", ::open, path, O_RDWR)};
    sys_call("flock", ::flock, fd.get(), LOCK_EX);
    reap(spawn_contender());
    sys_call("flock", ::flock, fd.get(), LOCK_UN);
}

// ---------------------------------------------------------------------------
// d) close(fd) 隐式释放
// ---------------------------------------------------------------------------
void case_d() {
    banner("E1d  close(fd) 隐式释放:A close 后 B 立刻拿到");
    unique_fd fd{sys_call("open", ::open, path, O_RDWR)};
    sys_call("flock", ::flock, fd.get(), LOCK_EX);
    std::printf("  [%6ld ms] A 持锁\n", ms());
    reap(spawn_contender());
    fd.reset(); // close(fd),全程没有 LOCK_UN
    std::printf("  [%6ld ms] A close(fd)\n", ms());
    reap(spawn_contender());
}

// ---------------------------------------------------------------------------
// e) fork:子进程继承同一打开文件描述
// ---------------------------------------------------------------------------
void case_e() {
    banner("E1e  fork 继承:锁属于打开文件描述,不属于进程");
    unique_fd fd{sys_call("open", ::open, path, O_RDWR)};
    sys_call("flock", ::flock, fd.get(), LOCK_EX);
    std::printf("  [%6ld ms] 父:持锁(fork 前)\n", ms());

    pid_t k = ::fork();
    if (k == 0) {                           // 子进程:继承的 fd 是同一个描述 → flock 直接成功
        int r = ::flock(fd.get(), LOCK_EX); // 阻塞版也不阻塞
        std::printf("  [%6ld ms] 子(pid=%d):继承 fd 上 flock(LOCK_EX) = %d(同一描述,不与父冲突)\n",
                    ms(), ::getpid(), r);
        int r2 = ::flock(fd.get(), LOCK_UN); // 子进程还能替父进程放锁
        std::printf("  [%6ld ms] 子:LOCK_UN = %d(放的是整个描述上的锁)\n", ms(), r2);
        ::_exit(0);
    }
    reap(k);
    reap(spawn_contender()); // 子进程已替父放锁 → 竞争者应拿到

    // 再锁一次,看「子进程 close 继承 fd」是否释放锁
    sys_call("flock", ::flock, fd.get(), LOCK_EX);
    std::printf("  [%6ld ms] 父:再次持锁\n", ms());
    pid_t k2 = ::fork();
    if (k2 == 0) {
        ::close(fd.get()); // 子进程关掉继承的 fd
        std::printf("  [%6ld ms] 子(pid=%d):close(继承 fd)\n", ms(), ::getpid());
        ::_exit(0);
    }
    reap(k2);
    reap(spawn_contender()); // 父的 fd 还引用着描述 → 锁应仍在
    fd.reset();
    reap(spawn_contender()); // 描述的最后一个引用关闭 → 锁应释放
}

} // namespace

int main() {
    // 子进程用 _exit 退场不会冲 stdio 缓冲;管道/重定向下还全程缓冲,
    // 时间线会乱 —— 干脆无缓冲,谁先拿到锁谁先落笔
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    prep_file();
    std::printf("pid=%d, 数据文件:%s(ext4)\n", ::getpid(), path);
    case_a();
    case_b();
    case_c();
    case_d();
    case_e();
    return 0;
}
