// E6 招牌封装:child_process —— 构造即 spawn,析构即收尸,move-only
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -o e6_child_process e6_child_process.cpp
// 设计条款:
//   1. 构造 = posix_spawn(E5 已证它是一发完成的安全通道),失败抛 std::system_error;
//   2. 析构 = 若子进程还活着:起手 SIGTERM 礼貌请退,100ms 不退就 SIGKILL 强杀,再 waitpid 收尸;
//      已死未收,只收尸;已收(detached),什么都不做——三种路径都有日志;
//   3. 拷贝删除、移动允许:进程句柄是独占资源,和 unique_ptr 一个道理;
//   4. wait() 阻塞收尸并解码退出状态;try_wait() 是 WNOHANG 版;
//   5. detach() 只解除"我负责收尸"的承诺(新进程组自立门户)。注意它的代价:
//      detached 子进程死在我们前面时,没人替它 wait,它会变僵尸,直到我们退出被收养。
//      生产上要么全局 SIGCHLD=SIG_IGN,要么让 detached 子进程永远比我们活得久。
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <system_error>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

extern char** environ;

class child_process {
  public:
    struct exit_status {
        bool signaled = false;
        int code = 0; // signaled=false:退出码(低 8 位);true:致死信号
        static exit_status decode(int raw) {
            exit_status s;
            if (WIFEXITED(raw)) {
                s.signaled = false;
                s.code = WEXITSTATUS(raw);
            } else if (WIFSIGNALED(raw)) {
                s.signaled = true;
                s.code = WTERMSIG(raw);
            }
            return s;
        }
        void print(const char* prefix) const {
            if (signaled)
                std::printf("%s被信号 %d 杀死\n", prefix, code);
            else
                std::printf("%s退出码 %d\n", prefix, code);
            std::fflush(stdout);
        }
    };
    static void print_status(const char* prefix, int raw) {
        exit_status::decode(raw).print(prefix);
    }

    child_process() = default;

    /// 构造即 spawn。args 至少含 argv[0]。detached=true 时进新进程组并放弃收尸责任。
    child_process(const std::string& path, std::vector<std::string> args, bool detached = false) {
        std::fflush(stdout); // 孩子继承 stdout 且直写 fd,提前把我们的缓冲冲出去防止反超
        std::vector<std::unique_ptr<char[]>> keep;
        keep.reserve(args.size());
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& a : args) {
            keep.push_back(std::make_unique<char[]>(a.size() + 1));
            std::memcpy(keep.back().get(), a.c_str(), a.size() + 1);
            argv.push_back(keep.back().get());
        }
        argv.push_back(nullptr);

        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
        short flags = 0;
        if (detached)
            flags |= POSIX_SPAWN_SETPGROUP; // 自立门户:新进程组(终端 ^C 打不到它)
        (void)posix_spawnattr_setflags(&attr, flags);

        int rc = posix_spawn(&pid_, path.c_str(), nullptr, &attr, argv.data(), environ);
        posix_spawnattr_destroy(&attr);
        if (rc != 0)
            throw std::system_error(rc, std::generic_category(), "posix_spawn(" + path + ")");
        detached_ = detached;
    }

    ~child_process() { dispose(); }

    child_process(const child_process&) = delete;
    child_process& operator=(const child_process&) = delete;

    child_process(child_process&& other) noexcept : pid_(other.pid_), detached_(other.detached_) {
        other.pid_ = -1;
        other.detached_ = false;
    }
    child_process& operator=(child_process&& other) noexcept {
        if (this != &other) {
            dispose();
            pid_ = other.pid_;
            detached_ = other.detached_;
            other.pid_ = -1;
            other.detached_ = false;
        }
        return *this;
    }

    /// 阻塞收尸,返回解码后的退出状态。重复调用抛 logic_error。
    [[nodiscard]] exit_status wait() {
        require_pid("wait");
        int raw = 0;
        if (waitpid(pid_, &raw, 0) != pid_)
            throw std::system_error(errno, std::generic_category(), "waitpid");
        pid_ = -1;
        return exit_status::decode(raw);
    }

    /// 非阻塞探测:孩子已死返回 true 并带出状态;还活着返回 false。
    bool try_wait(exit_status& out) {
        require_pid("try_wait");
        int raw = 0;
        pid_t r = waitpid(pid_, &raw, WNOHANG);
        if (r == pid_) {
            pid_ = -1;
            out = exit_status::decode(raw);
            return true;
        }
        if (r < 0)
            throw std::system_error(errno, std::generic_category(), "waitpid(WNOHANG)");
        return false;
    }

    /// 解除收尸承诺(见文件头条款 5)。
    void detach() noexcept { detached_ = true; }

    void terminate(int sig = SIGTERM) const {
        if (pid_ > 0)
            (void)kill(pid_, sig);
    }

    bool alive() const { return pid_ > 0 && kill(pid_, 0) == 0; }

    pid_t pid() const noexcept { return pid_; }
    bool joinable() const noexcept { return pid_ > 0; }

  private:
    pid_t pid_ = -1;
    bool detached_ = false;

    void require_pid(const char* who) const {
        if (pid_ <= 0)
            throw std::logic_error(std::string("child_process::") + who + ": 没有在管的子进程");
    }

    void dispose() noexcept { // 析构/移动赋值共用;不许抛
        if (pid_ <= 0)
            return;
        if (detached_) {
            log("  [~child_process] pid=", pid_, " 已 detach,析构不管它\n");
            pid_ = -1;
            return;
        }
        int raw = 0;
        if (waitpid(pid_, &raw, WNOHANG) == pid_) { // 已死未收,只差收尸
            log("  [~child_process] pid=", pid_, " 已死,直接收尸:");
            print_status("", raw);
            pid_ = -1;
            return;
        }
        log("  [~child_process] pid=", pid_, " 还活着 → SIGTERM\n");
        (void)kill(pid_, SIGTERM);
        for (int i = 0; i < 10; ++i) { // 给 100ms 优雅退场
            if (waitpid(pid_, &raw, WNOHANG) == pid_) {
                log("  [~child_process] pid=", pid_, " SIGTERM 生效,收尸完成\n");
                pid_ = -1;
                return;
            }
            usleep(10000);
        }
        log("  [~child_process] pid=", pid_, " 不肯退 → SIGKILL\n");
        (void)kill(pid_, SIGKILL);
        if (waitpid(pid_, &raw, 0) == pid_) {
            log("  [~child_process] pid=", pid_, " 已强杀+收尸\n");
        }
        pid_ = -1;
    }

    // 简易同步日志(析构路径不能指望缓冲顺序,逐条冲)
    template <typename... Rest> static void log(const char* a, long b, Rest... rest) {
        std::printf("%s%ld", a, b);
        std::fflush(stdout);
        if constexpr (sizeof...(rest) > 0)
            log(rest...);
    }
    static void log(const char* a) {
        std::printf("%s", a);
        std::fflush(stdout);
    }
};

// ---- 验证环境的小工具 ----
static bool proc_gone(pid_t p) {
    char path[64];
    std::snprintf(path, sizeof path, "/proc/%d", p);
    return access(path, F_OK) != 0;
}
static void show_state(pid_t p, const char* tag) {
    char path[64], line[128] = "(进程不在了)";
    std::snprintf(path, sizeof path, "/proc/%d/status", p);
    FILE* f = std::fopen(path, "r");
    if (f) {
        line[0] = '\0';
        while (std::fgets(line, sizeof line, f))
            if (std::strncmp(line, "State:", 6) == 0)
                break;
        std::fclose(f);
    }
    size_t n = std::strlen(line);
    std::printf("%s:%s%s", tag, line, (n > 0 && line[n - 1] == '\n') ? "" : "\n");
    std::fflush(stdout);
}

static void demo1_scope_exit_auto_reap() {
    std::printf("\n=== 演示 1:作用域结束自动收尸,不留僵尸 ===\n");
    pid_t cp = -1;
    {
        child_process c("/bin/sleep", {"sleep", "30"});
        cp = c.pid();
        std::printf("spawn /bin/sleep 30 → pid=%d\n", cp);
        usleep(50000); // 让它进入睡眠态再采样,State 更直观
        show_state(cp, "  作用域内");
    } // 析构在这里发生
    std::printf("  作用域已出,验证 /proc/%d:%s\n", cp,
                proc_gone(cp) ? "进程没了,僵尸零残留" : "还在(不该)");
}

static void demo2_early_kill() {
    std::printf("\n=== 演示 2:提前 kill 的清理路径 ===\n");
    child_process c("/bin/sleep", {"sleep", "30"});
    std::printf("spawn /bin/sleep 30 → pid=%d,200ms 后主动 SIGKILL\n", c.pid());
    usleep(200000);
    c.terminate(SIGKILL);
    child_process::exit_status s = c.wait(); // 主动收尸,析构就没事干了
    s.print("  wait() 解码:");
    std::printf("  (pid 已收,析构将走「没有在管的子进程」分支)\n");
}

static void demo3_move_only() {
    std::printf("\n=== 演示 3:move-only,进程句柄进容器 ===\n");
    static_assert(!std::is_copy_constructible_v<child_process>);
    static_assert(!std::is_copy_assignable_v<child_process>);
    static_assert(std::is_move_constructible_v<child_process>);
    std::printf("  static_assert 三条全过:拷贝删除,移动可用\n");
    std::vector<pid_t> pids;
    {
        std::vector<child_process> kids;
        kids.reserve(3);
        for (int i = 0; i < 3; ++i) {
            child_process c("/bin/sleep", {"sleep", "30"});
            kids.push_back(std::move(c)); // 进容器靠移动
            pids.push_back(kids.back().pid());
            std::printf("  第 %d 个孩子 pid=%d,被移动后原对象 pid()=%d(句柄已交接)\n", i + 1,
                        pids.back(), c.pid());
        }
        std::printf("  离开作用域,vector 析构逐个清理:\n");
    }
    int gone = 0;
    for (pid_t p : pids)
        gone += proc_gone(p) ? 1 : 0;
    std::printf("  验证:%d/%d 个孩子全部从 /proc 消失,零僵尸\n", gone, (int)pids.size());
}

static void demo4_normal_completion() {
    std::printf("\n=== 演示 4:孩子自己干完活,wait 拿退出码 ===\n");
    child_process c("/bin/echo", {"echo", "  [demo4] 我是子进程的输出,直接进父进程的 stdout"});
    child_process::exit_status s = c.wait();
    s.print("  wait() 解码:");
}

static void demo5_detach() {
    std::printf("\n=== 演示 5:detach——放它走,以及它必须知道的代价 ===\n");
    pid_t dp = -1;
    {
        child_process d("/bin/sleep", {"sleep", "2"}, /*detached=*/true);
        dp = d.pid();
        std::printf("spawn detached /bin/sleep 2 → pid=%d(已在新进程组)\n", dp);
        usleep(50000);
        show_state(dp, "  作用域内");
    } // 析构走 detach 分支,只解除承诺,不动孩子
    std::printf("  作用域出了,它还活着吗?%s\n", proc_gone(dp) ? "没了(不该)" : "还在跑");
    std::printf("  等 2.5s 让它自然退出。我们承诺过不 wait,它死在我们前面会怎样:\n");
    usleep(2500000);
    show_state(dp, "  它退出后的状态");
    std::printf("  僵尸出现了:detach 只是解除承诺,内核不会替我们 wait;要彻底免责,得全局\n");
    std::printf("  signal(SIGCHLD, SIG_IGN)(见 e3b 的附送场景)或保证它比我们活得久。\n");
}

int main() {
    std::printf("child_process:构造=spawn,析构=先礼后兵再收尸,move-only\n");
    demo1_scope_exit_auto_reap();
    demo2_early_kill();
    demo3_move_only();
    demo4_normal_completion();
    demo5_detach();
    std::printf(
        "\n收工:演示 1/3 的清理路径全部走完,僵尸零残留(演示 5 的代价场景除外,那是刻意的)\n");
    return 0;
}
