// E3c 退出码解码:正常退出、abort 自毁、被 kill,以及退出码只有 8 位的坑
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -o e3c_exit_status e3c_exit_status.cpp
// waitpid 拿到的 int 是内核打包的原始状态字,必须用宏解开:
//   WIFEXITED/WEXITSTATUS:正常退出,退出码在低 8 位
//   WIFSIGNALED/WTERMSIG:被信号杀死,记录的是致死信号
// 退出码本质只有 8 位:_exit(0x1234) 传出去,拿到手的只有 0x34——自己就被截断了。
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

static void report(const char* what, pid_t p, int raw) {
    if (WIFEXITED(raw))
        std::printf("  %-24s pid=%-7d WIFEXITED=1   WEXITSTATUS=%d (0x%02X)\n", what, p,
                    WEXITSTATUS(raw), WEXITSTATUS(raw));
    else if (WIFSIGNALED(raw))
        std::printf("  %-24s pid=%-7d WIFSIGNALED=1  WTERMSIG=%d (%s)\n", what, p, WTERMSIG(raw),
                    WTERMSIG(raw) == SIGABRT ? "SIGABRT" : "SIGKILL");
    else
        std::printf("  %-24s pid=%-7d 既非退出也非信号(不应发生)\n", what, p);
    std::fflush(stdout);
}

static void run_scenario(const char* what, void (*child_body)()) {
    pid_t p = fork();
    if (p == 0)
        child_body(); // 各场景的子进程在这里走完自己的一生
    int raw = 0;
    if (waitpid(p, &raw, 0) != p) {
        std::perror("waitpid");
        return;
    }
    report(what, p, raw);
}

static void die_by_kill_child() {
    // 子进程安静待死;父进程 kill 上来
    for (;;)
        pause();
}

int main() {
    std::printf("四种死法,同一个 waitpid,靠宏分辨:\n");

    run_scenario("_exit(42)", [] { _exit(42); });

    run_scenario("_exit(0x1234) 截断", [] { _exit(0x1234); });

    run_scenario("abort() 自毁", [] {
        std::printf("  [子] abort() 前先喊一嗓子\n");
        std::fflush(stdout);
        abort(); // SIGABRT,不会经过我们的任何 return
    });

    // 被父进程 kill:子进程进死循环,父进程 100ms 后补一刀 SIGKILL
    pid_t victim = fork();
    if (victim == 0)
        die_by_kill_child();
    usleep(100000);
    kill(victim, SIGKILL);
    int raw = 0;
    if (waitpid(victim, &raw, 0) == victim)
        report("父进程 kill -9", victim, raw);

    std::printf("解读:退出码与信号共用 waitpid 的原始状态字,不查 WIFSIGNALED 直接\n");
    std::printf("      WEXITSTATUS,会把被信号杀死读成退出码 0 之外的古怪值;\n");
    std::printf("      shell 里 $? 同样只有低 8 位(exit 256 与 exit 0 难分)。\n");
    return 0;
}
