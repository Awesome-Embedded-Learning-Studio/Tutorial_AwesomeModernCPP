// E2:sys_call 两种口味(异常版/expected 版)+ EINTR 自动重试 + SA_RESTART 对比
// 编译:g++ -std=c++23 -Wall -Wextra e2_eintr_retry.cpp -o e2
//   (std::expected 需要 C++23;实测 g++ 16.2.1 在 -std=c++20 下报
//    "'std::expected' is only available from C++23 onwards")
//
// 跑法:./e2 norestart   sigaction 不带 SA_RESTART:read 被信号打断返回 -1/EINTR,
//                            sys_call 在内部重试,第二次 read 成功
//      ./e2 restart     sigaction 带 SA_RESTART:内核替咱们重启 read,
//                            用户态根本看不到 EINTR,一次语义到底
//
// 编排:父进程阻塞 read 管道读端(慢速 fd,无数据永久阻塞);
//       子进程睡 200ms 后 kill(SIGUSR1) 打断父进程,再睡 200ms 后写入 "ping"。
//       两种模式下最终都读到数据——差别在"中间有没有发生过 EINTR"。
//       g_eintr_retries 是 stdout 侧的可观测证据,strace 侧见 .strace 文件。
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <expected>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <system_error>
#include <time.h>
#include <unistd.h>

// 公共工具(系列沿用)
std::error_code errno_code() noexcept {
    return std::error_code{errno, std::generic_category()};
}

volatile sig_atomic_t g_signal_count = 0;
static void on_signal(int) {
    g_signal_count += 1;
}

int g_eintr_retries = 0; // sys_call 内部消化掉的重试次数

// 口味一:异常版。EINTR 不算错,从头再来;其余 errno 装箱抛 system_error
template <class F, class... Args> auto sys_call(const char* what, F&& f, Args&&... args) {
    for (;;) {
        auto r = std::forward<F>(f)(std::forward<Args>(args)...);
        if (r == -1) {
            if (errno == EINTR) {
                ++g_eintr_retries;
                continue;
            }
            throw std::system_error{errno, std::generic_category(), what};
        }
        return r; // 0(EOF)不是 -1,原样放行
    }
}

// 口味二:expected 版。失败不抛,error_code 交给调用方处置
template <class F, class... Args> auto sys_call_expected(F&& f, Args&&... args)
    -> std::expected<decltype(std::forward<F>(f)(std::forward<Args>(args)...)), std::error_code> {
    using R = decltype(std::forward<F>(f)(std::forward<Args>(args)...));
    for (;;) {
        R r = std::forward<F>(f)(std::forward<Args>(args)...);
        if (r == -1) {
            if (errno == EINTR) {
                ++g_eintr_retries;
                continue;
            }
            return std::unexpected(errno_code());
        }
        return r;
    }
}

static void nap(long ms) {
    struct timespec ts{0, ms * 1000000L};
    struct timespec rem{};
    while (nanosleep(&ts, &rem) == -1 && errno == EINTR) {
        ts = rem; // 睡眠自己也可能被信号打断,补足剩余时间
    }
}

int main(int argc, char** argv) {
    bool restart = (argc > 1 && std::strcmp(argv[1], "restart") == 0);
    std::printf("mode = %s (SA_RESTART %s)\n", restart ? "restart" : "norestart",
                restart ? "ON : kernel restarts read for us" : "OFF: read returns -1/EINTR");

    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    if (restart)
        sa.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &sa, nullptr) == -1) {
        perror("sigaction");
        return 1;
    }

    int fds[2];
    if (pipe(fds) == -1) {
        perror("pipe");
        return 1;
    }

    pid_t pid = fork();
    if (pid == 0) { // 子进程:先打断,后投喂数据
        ::close(fds[0]);
        nap(200); // 等父进程进入 read
        kill(getppid(), SIGUSR1);
        nap(200); // 留出「打断 -> (重试) -> 再阻塞」的窗口
        const char msg[] = "ping\n";
        ssize_t w = ::write(fds[1], msg, sizeof msg - 1);
        (void)w;
        ::close(fds[1]);
        _exit(0);
    }
    ::close(fds[1]);

    char buf[64];
    // 主戏:慢速 fd 上的阻塞 read,交给异常版 sys_call
    ssize_t n = sys_call("read", ::read, fds[0], buf, sizeof buf);
    std::printf("read returned %zd bytes: \"%.*s\"\n", n, (int)n, buf);
    std::printf("signal delivered = %d, EINTR retries inside sys_call = %d\n", (int)g_signal_count,
                g_eintr_retries);

    // 口味二登场:再读一次,这次等 EOF(子进程已关写端)
    auto second = sys_call_expected(::read, fds[0], buf, sizeof buf);
    if (second)
        std::printf("expected flavor: second read = %zd (EOF passthrough, 0 is not -1)\n", *second);
    else
        std::printf("expected flavor: second read failed: %s\n", second.error().message().c_str());
    ::close(fds[0]);
    int st = 0;
    waitpid(pid, &st, 0);

    // 顺手验一次异常版的错误路径:what 前缀 + errno 装箱
    try {
        int bad = sys_call("open", ::open, "/tmp/errpar/e2_definitely_missing", O_RDONLY);
        (void)bad;
    } catch (const std::system_error& e) {
        std::printf("flavor-one error path: %s\n", e.what());
    }
    return 0;
}
