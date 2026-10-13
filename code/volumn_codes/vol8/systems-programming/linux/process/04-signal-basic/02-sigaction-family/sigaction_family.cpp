// E2:sigaction 全家福——SA_SIGINFO(谁发的信号能查到) / sa_mask(handler 期间自动屏蔽) /
//     SA_NODEFER(同信号嵌套) / SA_RESETHAND(一次性 handler) / signal() 的 glibc BSD 语义 /
//     sigprocmask 三件套 BLOCK/UNBLOCK/SETMASK 差异
// 编译:g++ -std=c++20 -Wall -Wextra -O2 sigaction_family.cpp -o sigaction_family
//
// SA_RESTART 不在此重做——thinking/02 的 E2 已给 strace 证据链
// (eintr_norestart.out 的 rt_sigreturn = -1 EINTR vs restart 版内核代重启),此处只引用。
#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/time.h> // setitimer/itimerval
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

void hwrite(const char* s) {
    write(STDOUT_FILENO, s, strlen(s));
}

void msleep(long ms) {
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}

// --------- [E2.1] SA_SIGINFO:handler 拿 siginfo_t,能查出"谁发的"
// 注:handler 里用 snprintf 格式化再 write。snprintf 不在 POSIX 的异步信号安全表里
// (man 7 signal-safety),这里为了把 si_* 字段原样打出来才用它。严格守法的写法
// (只 write 常量串)在 E3 的安全版里。本段关注"能查到什么",不是"怎么输出"。
const char* si_code_name(int code) {
    switch (code) {
        case SI_USER:
            return "SI_USER   kill()发来";
        case SI_TKILL:
            return "SI_TKILL  raise/tgkill发来";
        case SI_KERNEL:
            return "SI_KERNEL 内核产生";
        default:
            return "(other)";
    }
}

void on_info(int sig, siginfo_t* info, void*) {
    char buf[160];
    int n = std::snprintf(buf, sizeof buf,
                          "    [H] caught %d: si_signo=%d si_code=%d(%s) "
                          "si_pid=%ld si_uid=%ld\n",
                          sig, info->si_signo, info->si_code, si_code_name(info->si_code),
                          (long)info->si_pid, (long)info->si_uid);
    write(STDOUT_FILENO, buf, n);
}

// --------- [E2.2] sa_mask:handler 执行期间额外屏蔽的信号集
void on_usr2(int) {
    hwrite("    [H] SIGUSR2 handler entered\n");
    // 读当前进程屏蔽字:未设 SA_NODEFER,自身 SIGUSR2 自动在内,sa_mask 里的 SIGUSR1 也在
    sigset_t cur{};
    sigprocmask(0, nullptr, &cur);
    char buf[96];
    int n = std::snprintf(buf, sizeof buf,
                          "    [H] mask inside handler: SIGUSR2=%d SIGUSR1=%d "
                          "(self + sa_mask both blocked)\n",
                          sigismember(&cur, SIGUSR2), sigismember(&cur, SIGUSR1));
    write(STDOUT_FILENO, buf, n);

    raise(SIGUSR1); // handler 里给自己发 SIGUSR1——被 sa_mask 挡住,只 pending 不投递
    sigset_t pend{};
    sigpending(&pend);
    n = std::snprintf(buf, sizeof buf,
                      "    [H] raise(SIGUSR1) inside handler -> pending=%d "
                      "(NOT delivered while we run)\n",
                      sigismember(&pend, SIGUSR1));
    write(STDOUT_FILENO, buf, n);
    msleep(50); // 多待一会儿,证明整个 handler 期间它都进不来
    hwrite("    [H] SIGUSR2 handler returning\n");
}

void on_usr1_late(int) {
    hwrite("    [H] SIGUSR1 handler runs only AFTER SIGUSR2 handler returned\n");
}

// --------- [E2.3] SA_NODEFER:handler 执行期间不自动屏蔽自身,可重入
volatile sig_atomic_t g_depth = 0;
void on_nodefer(int) {
    g_depth += 1;
    char buf[64];
    int n = std::snprintf(buf, sizeof buf, "    [H] enter, depth=%d\n", (int)g_depth);
    write(STDOUT_FILENO, buf, n);
    if (g_depth == 1)
        raise(SIGUSR1); // 第一层里再发:SA_NODEFER 让它立刻递归进来,而非 pending
    hwrite("    [H] leave\n");
    g_depth -= 1;
}

// --------- [E2.5] signal() 读回与持久性
volatile sig_atomic_t g_sv_count = 0;
void on_signal_style(int) {
    g_sv_count += 1;
}

// --------- [E2.6] sigprocmask 三件套
void dump_mask(const char* tag) {
    sigset_t cur{};
    sigprocmask(0, nullptr, &cur);
    std::printf("    mask %-34s SIGINT=%d SIGTERM=%d SIGUSR1=%d SIGUSR2=%d\n", tag,
                sigismember(&cur, SIGINT), sigismember(&cur, SIGTERM), sigismember(&cur, SIGUSR1),
                sigismember(&cur, SIGUSR2));
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);

    // ============================================ [E2.1] SA_SIGINFO:谁发的
    std::printf("[E2.1] SA_SIGINFO: sender is identifiable, my pid=%ld uid=%ld\n", (long)getpid(),
                (long)getuid());
    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = on_info;
    sigaction(SIGUSR1, &sa, nullptr);

    raise(SIGUSR1); // glibc 的 raise 走 tgkill -> si_code=SI_TKILL,si_pid=自己

    pid_t pid = fork();
    if (pid == 0) {
        msleep(30);
        kill(getppid(), SIGUSR1); // 别的进程发 -> si_code=SI_USER,si_pid=对方 pid
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    std::printf("[E2.1] child pid=%ld sent it -> si_pid must equal this\n", (long)pid);

    // 内核自己产生的信号:itimer 到期发 SIGALRM -> SI_KERNEL,si_pid=0(没有发送者进程)
    struct sigaction sa_alrm{};
    sigemptyset(&sa_alrm.sa_mask);
    sa_alrm.sa_flags = SA_SIGINFO;
    sa_alrm.sa_sigaction = on_info;
    sigaction(SIGALRM, &sa_alrm, nullptr);
    struct itimerval it{};
    it.it_value.tv_usec = 50 * 1000; // 50ms 一次性
    setitimer(ITIMER_REAL, &it, nullptr);
    msleep(80);

    // ============================================ [E2.2] sa_mask
    std::printf("\n[E2.2] sa_mask={SIGUSR1}: USR1 cannot preempt USR2 handler\n");
    struct sigaction sa2{};
    sigemptyset(&sa2.sa_mask);
    sigaddset(&sa2.sa_mask, SIGUSR1); // handler 执行期间额外屏蔽 SIGUSR1
    sa2.sa_handler = on_usr2;
    sa2.sa_flags = 0;
    sigaction(SIGUSR2, &sa2, nullptr);

    struct sigaction sa1{};
    sigemptyset(&sa1.sa_mask);
    sa1.sa_handler = on_usr1_late;
    sigaction(SIGUSR1, &sa1, nullptr);

    raise(SIGUSR2);
    std::printf("[E2.2] back in main\n");

    // ============================================ [E2.3] SA_NODEFER
    std::printf("\n[E2.3] SA_NODEFER: same signal re-enters its own handler\n");
    struct sigaction sa3{};
    sigemptyset(&sa3.sa_mask);
    sa3.sa_handler = on_nodefer;
    sa3.sa_flags = SA_NODEFER;
    sigaction(SIGUSR1, &sa3, nullptr);
    raise(SIGUSR1);
    std::printf("[E2.3] done (depth back to %d; without SA_NODEFER the inner "
                "raise would stay pending)\n",
                (int)g_depth);

    // ============================ [E2.4] SA_RESETHAND:一次性 handler(在子进程里跑,方便收尸)
    std::printf("\n[E2.4] SA_RESETHAND: one-shot handler, 2nd delivery hits "
                "SIG_DFL\n");
    pid = fork();
    if (pid == 0) {
        setvbuf(stdout, nullptr, _IONBF, 0);
        struct sigaction sac{};
        sigemptyset(&sac.sa_mask);
        sac.sa_handler = [](int) { hwrite("    [H] first delivery: handler body runs\n"); };
        sac.sa_flags = SA_RESETHAND;
        sigaction(SIGUSR1, &sac, nullptr);
        raise(SIGUSR1);
        hwrite("CHILD: survived 1st; raising again with disposition reset to "
               "SIG_DFL (terminate)\n");
        raise(SIGUSR1); // 第二次:处置已被重置为默认,SIGUSR1 默认动作 = 终止进程
        hwrite("CHILD: never printed (dead)\n");
        _exit(0);
    }
    waitpid(pid, &st, 0);
    std::printf("[E2.4] child ended: WIFSIGNALED=%d WTERMSIG=%d (SIGUSR1=%d) "
                "-> reset happened\n",
                WIFSIGNALED(st), WTERMSIG(st), SIGUSR1);

    // ============================================ [E2.5] signal() 的 glibc 语义
    std::printf("\n[E2.5] signal(): glibc installs BSD semantics\n");
    g_sv_count = 0;
    sighandler_t old = ::signal(SIGUSR1, on_signal_style);
    std::printf("[E2.5] signal() returned old disposition: %s\n",
                old == SIG_ERR ? "SIG_ERR" : (old == SIG_DFL ? "SIG_DFL" : "(non-null)"));

    // 读回:glibc 的 signal() 是 sigaction 的薄封装,flags 能看出它装了什么
    struct sigaction rb{};
    sigaction(SIGUSR1, nullptr, &rb);
    std::printf("[E2.5] readback sa_flags=0x%x: SA_RESTART=%d SA_NODEFER=%d "
                "SA_RESETHAND=%d\n",
                rb.sa_flags, !!(rb.sa_flags & SA_RESTART), !!(rb.sa_flags & SA_NODEFER),
                !!(rb.sa_flags & SA_RESETHAND));

    raise(SIGUSR1);
    raise(SIGUSR1);
    std::printf("[E2.5] raised twice -> handler count=%d; handler PERSISTS "
                "(System V semantics would reset after 1st and die on 2nd)\n",
                (int)g_sv_count);

    // ============================================ [E2.6] sigprocmask 三件套
    std::printf("\n[E2.6] sigprocmask: BLOCK=并集, UNBLOCK=差集, SETMASK=整个替换\n");
    sigset_t s{};
    sigemptyset(&s);
    dump_mask("initial (empty)");

    sigaddset(&s, SIGUSR2);
    sigprocmask(SIG_BLOCK, &s, nullptr); // BLOCK:mask |= s
    dump_mask("BLOCK{USR2}");

    sigaddset(&s, SIGUSR1);
    sigprocmask(SIG_BLOCK, &s, nullptr); // BLOCK{USR1,USR2}:mask |= {两者}
    dump_mask("BLOCK{USR1,USR2}");

    sigemptyset(&s);
    sigaddset(&s, SIGUSR2);
    sigprocmask(SIG_UNBLOCK, &s, nullptr); // UNBLOCK:mask -= {USR2},USR1 留下
    dump_mask("UNBLOCK{USR2}");

    sigemptyset(&s);
    sigprocmask(SIG_SETMASK, &s, nullptr); // SETMASK:mask = {} 全清
    dump_mask("SETMASK{} (clear all)");
    return 0;
}
