// E6:terminal_guard——设置挂在内核的 tty 上,不挂在进程上;谁改的谁负责收回
//   阶段A  无 guard:孩子A 把 tty 改成 raw 后直接 _exit。收尸后父进程一查,raw 还在;
//          接力的孩子B 什么都没设,读到的却是 raw 的逐字节——状态跨进程存活,进程死了设置还在
//   阶段B  有 guard:同样设 raw,函数正常返回,析构把 termios 还原。前后 lflag 对表
//   阶段C  异常路径:guard 存在期间 throw,栈展开时析构照样还原——RAII 在异常下的意义
//   阶段D  TCSAFLUSH vs TCSANOW:guard 收回时顺手把没读的输入冲不冲。
//          D1 用 TCSAFLUSH:滞留的 "junk" 被冲,后来者只读到 "z\n";
//          D2 用 TCSANOW:滞留的 "junk" 还在,后来者一次读到 "junkz\n"
#include <fcntl.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>
#include <sys/wait.h>

// 本篇的 C++ 落点:一个最小的 terminal_guard。
// 构造时存底,析构时还原,还原用 TCSAFLUSH(顺手冲掉没收的输入)。
// 析构在异常展开时也会跑,这就是“谁改的谁负责收回”的机制保证。
class terminal_guard {
  public:
    explicit terminal_guard(int fd, int restore_actions = TCSAFLUSH)
        : fd_(fd), restore_actions_(restore_actions) {
        if (tcgetattr(fd_, &saved_) != 0)
            throw std::runtime_error(std::string("tcgetattr: ") + std::strerror(errno));
    }
    void apply_raw() {
        termios t = saved_;
        cfmakeraw(&t);
        if (tcsetattr(fd_, TCSANOW, &t) != 0)
            throw std::runtime_error(std::string("tcsetattr: ") + std::strerror(errno));
    }
    ~terminal_guard() { tcsetattr(fd_, restore_actions_, &saved_); }
    terminal_guard(const terminal_guard&) = delete;
    terminal_guard& operator=(const terminal_guard&) = delete;

  private:
    int fd_;
    int restore_actions_;
    termios saved_{};
};

static std::string lflag_of(int sfd) {
    termios t{};
    tcgetattr(sfd, &t);
    char b[96];
    std::snprintf(b, sizeof b, "lflag=0x%lx ICANON=%d ECHO=%d ISIG=%d", (unsigned long)t.c_lflag,
                  (int)!!(t.c_lflag & ICANON), (int)!!(t.c_lflag & ECHO),
                  (int)!!(t.c_lflag & ISIG));
    return b;
}

static std::string hexof(const unsigned char* p, int n) {
    std::string s;
    for (int i = 0; i < n; ++i)
        s += (char)p[i];
    return "(" + s + ")";
}

// 接力读端的上报格式:n<0 表示带的是异常文案的长度
struct Report {
    int n;
    unsigned char buf[256];
};

int main() {
    // ---------- 阶段A:无 guard,状态跨进程存活 ----------
    {
        int mfd, sfd;
        openpty(&mfd, &sfd, nullptr, nullptr, nullptr);
        std::printf("[阶段A 无 guard:改完就跑,raw 留在 tty 上]\n");
        std::printf("  初始 %s\n", lflag_of(sfd).c_str());

        pid_t a = fork();
        if (a == 0) { // 孩子A:设 raw,不给任何善后
            close(mfd);
            dup2(sfd, 0);
            close(sfd);
            termios t{};
            tcgetattr(0, &t);
            cfmakeraw(&t);
            tcsetattr(0, TCSANOW, &t);
            _exit(0); // 模拟“崩了没还原”
        }
        waitpid(a, nullptr, 0);
        std::printf("  孩子A(_exit 无善后)之后 %s\n", lflag_of(sfd).c_str());

        // 孩子B 接力:什么也不设,读 2 次
        int rp[2];
        pipe(rp);
        pid_t b = fork();
        if (b == 0) {
            close(mfd);
            close(rp[0]);
            dup2(sfd, 0);
            close(sfd);
            for (int i = 0; i < 2; ++i) {
                Report r{};
                r.n = read(0, r.buf, 256);
                if (write(rp[1], &r, sizeof r) < 0 || r.n <= 0)
                    break;
            }
            close(rp[1]);
            _exit(0);
        }
        close(rp[1]);
        close(sfd);
        write(mfd, "a", 1);
        usleep(200 * 1000);
        write(mfd, "b", 1);
        Report r;
        while (read(rp[0], &r, sizeof r) == (ssize_t)sizeof r)
            std::printf("  孩子B(没设过任何东西)读到 n=%d %s —— 继承了 raw,没换行也到货\n", r.n,
                        hexof(r.buf, r.n).c_str());
        waitpid(b, nullptr, 0);
        close(rp[0]);
        close(mfd);
        std::printf("\n");
    }

    // ---------- 阶段B/C:guard 的正常路径与异常路径 ----------
    for (int variant = 0; variant < 2; ++variant) {
        int mfd, sfd;
        openpty(&mfd, &sfd, nullptr, nullptr, nullptr);
        std::printf(variant == 0 ? "[阶段B 有 guard,正常返回:析构还原]\n"
                                 : "[阶段C 有 guard,中途 throw:栈展开照样还原]\n");
        std::printf("  初始 %s\n", lflag_of(sfd).c_str());
        int rp[2];
        pipe(rp);
        pid_t c = fork();
        if (c == 0) {
            close(mfd);
            close(rp[0]);
            dup2(sfd, 0);
            close(sfd);
            Report r{};
            r.n = 0;
            try {
                terminal_guard g(0);
                g.apply_raw();
                if (variant == 1)
                    throw std::runtime_error("模拟业务炸了");
            } catch (const std::exception& e) {
                r.n = -(int)std::strlen(e.what());
                std::memcpy(r.buf, e.what(), (size_t)(-r.n));
            }
            write(rp[1], &r, sizeof r);
            close(rp[1]);
            _exit(0);
        }
        close(rp[1]);
        usleep(120 * 1000);
        std::printf("  孩子%s后 %s\n", variant == 0 ? "正常返回" : "抛异常被接住",
                    lflag_of(sfd).c_str());
        Report r;
        read(rp[0], &r, sizeof r);
        if (r.n < 0)
            std::printf("  孩子报告的异常:%s\n", std::string((char*)r.buf, (size_t)(-r.n)).c_str());
        waitpid(c, nullptr, 0);
        close(rp[0]);
        close(sfd);
        close(mfd);
        std::printf("\n");
    }

    // ---------- 阶段D:TCSAFLUSH 冲不冲滞留输入 ----------
    for (int variant = 0; variant < 2; ++variant) {
        int mfd, sfd;
        openpty(&mfd, &sfd, nullptr, nullptr, nullptr);
        std::printf(variant == 0 ? "[阶段D1 guard 用 TCSAFLUSH:还原时冲掉没读的输入]\n"
                                 : "[阶段D2 guard 用 TCSANOW:还原时保留没读的输入]\n");
        pid_t c = fork();
        if (c == 0) {
            close(mfd);
            dup2(sfd, 0);
            close(sfd);
            try {
                terminal_guard g(0, variant == 0 ? TCSAFLUSH : TCSANOW);
                g.apply_raw();
                usleep(200 * 1000); // 期间父进程写入的 junk 会滞留在队列里
                throw std::runtime_error("boom");
            } catch (const std::exception&) {
            }
            _exit(0);
        }
        usleep(100 * 1000);
        write(mfd, "junk", 4); // 孩子正 raw 睡着,这 4 字节没人读
        usleep(250 * 1000);    // 等孩子退干净,guard 已还原
        waitpid(c, nullptr, 0);

        // 接力读端读 1 次(canonical 已还原;父进程一直握着 sfd,slave 没断过粮)
        int rp2[2];
        pipe(rp2);
        pid_t b = fork();
        if (b == 0) {
            close(mfd);
            close(rp2[0]);
            dup2(sfd, 0);
            close(sfd);
            Report r{};
            r.n = read(0, r.buf, 256);
            write(rp2[1], &r, sizeof r);
            close(rp2[1]);
            _exit(0);
        }
        close(rp2[1]);
        close(sfd);
        usleep(100 * 1000);
        write(mfd, "z\n", 2); // 给一个 canonical 的行结束
        Report r;
        while (read(rp2[0], &r, sizeof r) == (ssize_t)sizeof r)
            std::printf("  接力读端读到 n=%d %s —— %s\n", r.n, hexof(r.buf, r.n).c_str(),
                        variant == 0 ? "junk 被 TCSAFLUSH 冲了" : "junk 滞留,混进了这一行");
        waitpid(b, nullptr, 0);
        close(rp2[0]);
        close(mfd);
        std::printf("\n");
    }
    return 0;
}
