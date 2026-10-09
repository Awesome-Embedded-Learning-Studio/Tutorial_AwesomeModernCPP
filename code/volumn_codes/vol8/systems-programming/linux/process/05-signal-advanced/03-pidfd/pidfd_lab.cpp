// E3 pidfd:把「进程」变成一个可 poll 的句柄(《信号(下):实时信号、signalfd 与 pidfd》E3)
//
// 五组观察:
//   a) 第三种收尸:pidfd_open 子进程 → poll 等 POLLIN → waitid(P_PIDFD) 拿退出状态。
//      不装 SIGCHLD handler、不轮询 waitpid——「句柄可读 = 目标退出」和 fd 语义统一;
//      收尸后同一 pidfd 再 poll 得到 POLLHUP、再 waitid 得 ECHILD,死即永死
//   b) pidfd_send_signal:按句柄发信号(可带 sigqueue 同款数据),收方 sigwaitinfo 读回
//   c) pidfd_getfd:父进程凭句柄拿到子进程 fd 表里的 fd(pread 出子进程写的标记);
//      兄弟进程之间同样操作 → EPERM(yama ptrace_scope=1:只许对后代)
//   d) pid 复用竞态:新 pid+user namespace 里把 pid_max 调小,真实复现「数字 pid 被
//      回收再分配给新进程」:kill(老数字) 误伤无辜新进程,pidfd_send_signal(老句柄)
//      返回 ESRCH——句柄不受数字复用的影响,这是 pidfd 的立身之本
//   e) 对比:waitpid(数字)/SIGCHLD(信号)/pidfd(句柄) 三条收尸路径(README 有表)
//
// 编译:g++ -std=c++20 -Wall -Wextra -O2 pidfd_lab.cpp -o pidfd_lab && ./pidfd_lab
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <initializer_list>
#include <poll.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

timespec t0 = [] {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
}();

long ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec - t0.tv_sec) * 1000 + (ts.tv_nsec - t0.tv_nsec) / 1000000;
}

void msleep(long v) {
    timespec d{v / 1000, (v % 1000) * 1000000L};
    nanosleep(&d, nullptr);
}

const char* marker_path = "/home/charliechen/lp05_scratch/e3/marker.txt"; // pidfd_getfd 的靶子

void ensure_marker() // 复现时自动补靶子文件
{
    FILE* f = fopen(marker_path, "r");
    if (f) {
        fclose(f);
        return;
    }
    f = fopen(marker_path, "w");
    if (f) {
        fputs("PIDFD-GETFD-MARKER-31415926\n", f);
        fclose(f);
    }
}

// glibc 2.36+ 有 pidfd_open/pidfd_send_signal/pidfd_getfd 封装(<sys/pidfd.h>);
// 这里走裸 syscall,把「它们就是 434/424/438 三个系统调用」摆在明面上(见 E6 表)
int pidfd_open_(pid_t pid, unsigned int flags) {
    return (int)syscall(SYS_pidfd_open, pid, flags);
}
int pidfd_send_signal_(int pidfd, int sig, siginfo_t* info, unsigned int flags) {
    return (int)syscall(SYS_pidfd_send_signal, pidfd, sig, info, flags);
}
int pidfd_getfd_(int pidfd, int target_fd, unsigned int flags) {
    return (int)syscall(SYS_pidfd_getfd, pidfd, target_fd, flags);
}

int wait_pidfd(int pidfd, int* exit_status) // poll(POLLIN) + waitid(P_PIDFD)
{
    pollfd p{pidfd, POLLIN, 0};
    int r = ::poll(&p, 1, 2000);
    if (r <= 0)
        return r;
    siginfo_t si{};
    si.si_pid = 0;
    if (waitid((idtype_t)P_PIDFD, (id_t)pidfd, &si, WEXITED) != 0)
        return -1;
    if (exit_status)
        *exit_status = si.si_status;
    return 1;
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0); // 多进程实验:别让缓冲区吃掉输出
    ensure_marker();
    std::printf("== E3 pidfd:进程句柄化 ==\n");

    // ---- a) poll(pidfd) 等退出 + waitid(P_PIDFD) 收尸 ----------------------------
    std::printf("\n[a] 第三种收尸:pidfd + poll + waitid(P_PIDFD)\n");
    pid_t c1 = ::fork();
    if (c1 == 0) {
        msleep(300);
        _exit(42); // 退出码 42
    }
    int afd = pidfd_open_(c1, 0);
    std::printf("    fork 出 c1(pid=%d),pidfd_open → fd=%d;子进程 300ms 后退出\n", (int)c1, afd);
    {
        pollfd p{afd, POLLIN, 0};
        int early = ::poll(&p, 1, 50); // 子进程还活着:POLLIN 不该来
        std::printf("    t=%3ldms poll(pidfd, 50ms)=%d(活着时无事件)\n", ms(), early);
    }
    int status = -1;
    int got = wait_pidfd(afd, &status);
    std::printf("    t=%3ldms poll 就绪,waitid(P_PIDFD) → 退出码 %d(si_code 路径:WEXITED)\n", ms(),
                status);
    (void)got;
    {
        pollfd p{afd, POLLIN, 0};
        int after = ::poll(&p, 1, 0);
        std::printf("    收尸后再 poll(pidfd)=%d,revents=%#x(=%#x|%#x "
                    "POLLIN|POLLHUP:人已注销,句柄等价于挂断)\n",
                    after, p.revents, p.revents & POLLIN, p.revents & POLLHUP);
    }
    if (waitid((idtype_t)P_PIDFD, (id_t)afd, nullptr, WEXITED) == 0)
        std::printf("    (异常:二次 waitid 竟成功)\n");
    else
        std::printf("    二次 waitid → %s(尸已收过;waitpid 二次收也是这个错)\n",
                    std::strerror(errno));
    ::close(afd);

    // ---- b) pidfd_send_signal:按句柄发,带数据 -----------------------------------
    std::printf("\n[b] pidfd_send_signal(收方 sigwaitinfo 读回):\n");
    pid_t c2 = ::fork();
    if (c2 == 0) {
        sigset_t s;
        sigemptyset(&s);
        sigaddset(&s, SIGRTMIN + 1);
        sigprocmask(SIG_BLOCK, &s, nullptr);
        siginfo_t si{};
        int got_sig = sigwaitinfo(&s, &si);
        std::printf("    c2(pid=%d)收到 signo=%d,si_value=%d,发送方 si_pid=%d\n", (int)getpid(),
                    got_sig, si.si_value.sival_int, (int)si.si_pid);
        _exit(0);
    }
    {
        int f2 = pidfd_open_(c2, 0);
        msleep(50); // 等 c2 把信号挡住(不然默认动作直接收走)
        siginfo_t out{};
        out.si_signo = SIGRTMIN + 1;
        out.si_code = SI_QUEUE;
        out.si_value.sival_int = 777;
        out.si_pid = getpid(); // 注意:pidfd_send_signal 带 info 时内核不代填 si_pid,要自己写
        int r = pidfd_send_signal_(f2, SIGRTMIN + 1, &out, 0);
        std::printf("    父进程 pidfd_send_signal(句柄) 返回 %d(带 sival_int=777)\n", r);
        waitpid(c2, nullptr, 0);
        ::close(f2);
    }

    // ---- c) pidfd_getfd:拿别人的 fd ----------------------------------------------
    std::printf("\n[c] pidfd_getfd(本机 yama/ptrace_scope:");
    {
        FILE* y = fopen("/proc/sys/kernel/yama/ptrace_scope", "r");
        int scope = -1;
        if (y) {
            fscanf(y, "%d", &scope);
            fclose(y);
        }
        std::printf("%d):\n", scope);
    }
    int up[2], down[2]; // 两条单向管道:上行报 fd 号,下行发回执(共用一条会自己吃字节)
    pipe(up);
    pipe(down);
    pid_t c3 = ::fork();
    if (c3 == 0) { // 打开 marker 文件,把 fd 号报给父进程,等一个回执再退
        close(up[0]);
        close(down[1]);
        int m = open(marker_path, O_RDONLY);
        char line[64];
        int len = snprintf(line, sizeof line, "%d\n", m);
        write(up[1], line, (size_t)len);
        char ack{};
        read(down[0], &ack, 1); // 阻塞保活
        _exit(0);
    }
    {
        close(up[1]);
        close(down[0]);
        char line[64]{};
        read(up[0], line, sizeof line - 1);
        int c3pid = c3, c3fd = atoi(line);
        int f3 = pidfd_open_(c3pid, 0);
        int stolen = pidfd_getfd_(f3, c3fd, 0);
        char buf[64]{};
        pread(stolen, buf, sizeof buf - 1, 0);
        std::printf("    父进程:pidfd_getfd(c3 的 fd=%d) → %d,pread 出:「%s」\n", c3fd, stolen,
                    buf);
        ::close(stolen);
        ::close(f3);

        // 兄弟之间:c4 与 c3 同父,不是彼此的后代 → yama=1 下应 EPERM
        pid_t c4 = ::fork();
        if (c4 == 0) {
            int fs = pidfd_open_(c3pid, 0);
            int bad = pidfd_getfd_(fs, c3fd, 0);
            std::printf("    兄弟 c4:pidfd_getfd(c3 的 fd) → %d,errno=%s(非后代,被 yama 拦)\n", bad,
                        bad == -1 ? std::strerror(errno) : "无");
            _exit(0);
        }
        waitpid(c4, nullptr, 0);
        char ack = 'k';
        write(down[1], &ack, 1); // 放 c3 退场
        waitpid(c3, nullptr, 0);
    }

    // ---- d) pid 复用竞态(新 pidns 里调小 pid_max,真实复现) ----------------------
    std::printf("\n[d] pid 复用竞态:新 user+pid namespace,pid_max 调到 302:\n");
    pid_t racer = ::fork();
    if (racer == 0) {
        if (unshare(CLONE_NEWUSER | CLONE_NEWPID) != 0) {
            std::printf("    unshare 失败:%s(本项跳过)\n", std::strerror(errno));
            _exit(9);
        }
        pid_t w = ::fork(); // w 是新 pidns 的 1 号,新 userns 里能力齐全
        if (w == 0) {
            FILE* f = fopen("/proc/sys/kernel/pid_max", "w");
            if (!f || fputs("302\n", f) == EOF || fclose(f) != 0) {
                std::printf("    调小 pid_max 失败:%s\n", std::strerror(errno));
                _exit(9);
            }
            // 新 pidns 的分配器:先从 2 往上爬;游标一旦越过 300(RESERVED_PIDS),
            // 下限就固定在 300,低号段(2..299)从此不再分配——所以要先把低号烧完,
            // 让 A 落进会循环的 [300,301] 段,复用才会真实发生
            int burned = 0;
            for (;;) {
                pid_t t = ::fork();
                if (t == 0)
                    _exit(0);
                ++burned;
                bool high = t >= 300;
                waitpid(t, nullptr, 0);
                if (high)
                    break; // 游标已进高号段
            }
            std::printf(
                "    [ns 内] pid_max=302;先烧掉低号段(2..299 共 %d 个),让 A 落进会循环的段:\n",
                burned - 1);
            pid_t a = ::fork();
            if (a == 0)
                _exit(7);
            int af = pidfd_open_(a, 0);
            waitpid(a, nullptr, 0); // 收尸 → 数字 a 回池子
            std::printf("    [ns 内] A(pid=%d)已退出并收尸,pidfd=%d 还在手上\n", (int)a, af);

            int hand[2];
            pipe(hand); // 候选 B 挡好信号后握手
            pid_t victim = 0;
            int rounds = 0;
            for (;;) {
                pid_t p = ::fork();
                ++rounds;
                if (p == 0) { // 候选 B:挡住 SIGUSR1,握手,等信号来汇报
                    sigset_t s;
                    sigemptyset(&s);
                    sigaddset(&s, SIGUSR1);
                    sigprocmask(SIG_BLOCK, &s, nullptr);
                    write(hand[1], "r", 1);
                    siginfo_t si{};
                    int got_s = sigwaitinfo(&s, &si);
                    std::printf(
                        "    [ns 内] B(pid=%d)收到 signo=%d si_pid=%d —— 我不是 A,被误伤了!\n",
                        (int)getpid(), got_s, (int)si.si_pid);
                    _exit(0);
                }
                char r{};
                read(hand[0], &r, 1); // 等 B 就位再判断
                if (p == a || rounds > 4000) {
                    victim = p;
                    break;
                }
                kill(p, SIGTERM); // 不是那个数字:清场,继续绕圈
                waitpid(p, nullptr, 0);
            }
            if (victim == a)
                std::printf(
                    "    [ns 内] 第 %d 个候选 B 拿到了老数字 pid=%d —— 数字回来了,人不是 A\n",
                    rounds, (int)victim);
            else {
                std::printf("    [ns 内] 兜底触发(未复现,输出仅供参考)\n");
                _exit(0);
            }
            int k = kill(a, SIGUSR1); // 按数字发:发给的是无辜的 B
            siginfo_t out{};
            out.si_signo = SIGUSR1;
            out.si_code = SI_QUEUE;
            out.si_value.sival_int = 99;
            int h = pidfd_send_signal_(af, SIGUSR1, &out, 0);
            std::printf("    [ns 内] kill(数字 %d) 返回 %d → 误伤 B;pidfd_send_signal(老句柄) 返回 "
                        "%d,errno=%s\n",
                        (int)a, k, h, h == -1 ? std::strerror(errno) : "无错");
            std::printf("    [ns 内] 数字会被复用,句柄不会认错人:这就是 pidfd 的立身之本\n");
            waitpid(victim, nullptr, 0);
            _exit(0);
        }
        int st = 0;
        waitpid(w, &st, 0);
        _exit(WEXITSTATUS(st));
    }
    int rst = 0;
    waitpid(racer, &rst, 0);
    if (WEXITSTATUS(rst) == 9)
        std::printf("    (本项在此环境不可用)\n");

    std::printf("\nE3 done,t=%ldms\n", ms());
    return 0;
}
