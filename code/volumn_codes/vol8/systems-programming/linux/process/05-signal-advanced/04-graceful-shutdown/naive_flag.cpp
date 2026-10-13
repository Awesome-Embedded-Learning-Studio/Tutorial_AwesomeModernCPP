// E4-b 对照组:「handler 里设 flag」的朴素版两副面孔(《信号(下)》E4)
//
// 服务器骨架:主循环阻塞在 accept(),收到连接就干 50ms 活回一行;
// SIGTERM handler 只做异步信号安全的一件事:g_stop=1 + write 一行。
//
//   面孔一(SA_RESTART):accept 被信号打断后内核自动重启 → 主循环根本没机会看旗子。
//     时间线:SIGTERM 在 t=200 就到了,服务器却一路睡到 t=400 下一个连接来才醒,
//     还把那条不该接的连接接了、干了、回完,才注意到该退了 —— 关停延迟 + 误接客。
//   面孔二(不开 SA_RESTART):accept 返回 -1/EINTR → 主循环当场看旗子退场,
//     t=200 信号到、t=200 服务器关门;t=400 的连接被拒。
//   但面孔二仍要 handler+flag+EINTR 三件套配对,阻塞点一多(accept/read/recv 各一处)
//     每处都得记得查旗;signalfd 版(E4-a)把信号变成事件循环里的一个 fd,这套配对全省。
//
// 编译:g++ -std=c++20 -Wall -Wextra -O2 naive_flag.cpp -o naive_flag && ./naive_flag
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
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

volatile sig_atomic_t g_stop = 0;

void on_term(int) {
    g_stop = 1;
    const char m[] = "handler 跑了:g_stop=1(handler 里只有赋值+write 两件安全的事)\n";
    write(1, m, sizeof m - 1);
}

int make_listener() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    bind(fd, (sockaddr*)&addr, sizeof addr);
    listen(fd, 8);
    return fd;
}

// 客户端剧本:t=50 连一条;[kill_at] 时 SIGTERM;t=400 再连一条(看服务器接不接)
// 只生不收:父进程要先回去跑 accept 循环,收尾在 serve 之后
struct actors {
    pid_t sup, cli;
};
actors run_clients(int port, long kill_at, int lfd) {
    pid_t sup = fork();
    if (sup == 0) {
        close(lfd); // 子进程不接客,继承来的 listen fd 先关掉
        msleep(kill_at);
        kill(getppid(), SIGTERM);
        _exit(0);
    }
    pid_t cli = fork();
    if (cli == 0) {
        close(lfd); // 同上:客户端更不该拿着服务器的 listen fd
        signal(SIGPIPE, SIG_IGN);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        msleep(50);
        for (int i = 1; i <= 2; ++i) {
            if (i == 2)
                msleep(350); // 第二条压着 t=400 去
            int c = socket(AF_INET, SOCK_STREAM, 0);
            int r = connect(c, (sockaddr*)&addr, sizeof addr);
            if (r == 0) {
                write(c, "ping", 4);
                char b[64]{};
                ssize_t n = read(c, b, sizeof b - 1);
                b[n > 0 ? n : 0] = 0;
                std::printf("t=%3ldms [client] 第 %d 次 connect → 0,收到「%s」\n", ms(), i,
                            n > 0 ? b : "(无回音)");
                close(c);
            } else {
                std::printf("t=%3ldms [client] 第 %d 次 connect → -1(%s)\n", ms(), i,
                            std::strerror(errno));
            }
        }
        _exit(0);
    }
    return {sup, cli};
}

void reap(actors a) {
    waitpid(a.cli, nullptr, 0);
    waitpid(a.sup, nullptr, 0);
}

void serve(int lfd, bool restart, const char* label) {
    struct sigaction sa{};
    sa.sa_handler = on_term;
    sigemptyset(&sa.sa_mask);
    if (restart)
        sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, nullptr);

    int served = 0;
    for (;;) {
        int cfd = accept(lfd, nullptr, nullptr); // 阻塞版:朴素版的常态
        if (cfd < 0) {
            if (errno == EINTR) {
                std::printf("t=%3ldms [%s] accept 返回 EINTR → 当场看旗:g_stop=%d\n", ms(), label,
                            (int)g_stop);
                if (g_stop)
                    break;
                continue;
            }
            break;
        }
        ++served;
        msleep(50); // 干活
        write(cfd, "ack", 3);
        std::printf("t=%3ldms [%s] 接了第 %d 条连接,干完 50ms 活,回了 ack\n", ms(), label, served);
        close(cfd);
        if (g_stop) {
            std::printf("t=%3ldms [%s] 循环回来看旗:g_stop=1 → 退场(共接了 %d 条)\n", ms(), label,
                        served);
            break;
        }
    }
    close(lfd);
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* notice0 = "== E4-b 朴素版:handler 设 flag,主循环阻塞在 accept() ==\n"
                          "剧本两条连接:t=50 与 t=400;SIGTERM 在 t=200 到\n";
    write(1, notice0, strlen(notice0));

    // 面孔一:SA_RESTART
    g_stop = 0;
    int l1 = make_listener();
    sockaddr_in a1{};
    socklen_t al1 = sizeof a1;
    getsockname(l1, (sockaddr*)&a1, &al1);
    std::printf("\n[面孔一] SA_RESTART:信号打断 accept 后内核自动重启,旗子没人看\n");
    actors act1 = run_clients(ntohs(a1.sin_port), 200, l1);
    serve(l1, true, "面孔一");
    reap(act1);

    // 面孔二:不开 SA_RESTART
    g_stop = 0;
    int l2 = make_listener();
    sockaddr_in a2{};
    socklen_t al2 = sizeof a2;
    getsockname(l2, (sockaddr*)&a2, &al2);
    std::printf("\n[面孔二] 不开 SA_RESTART:accept 返回 EINTR,主循环当场看旗\n");
    actors act2 = run_clients(ntohs(a2.sin_port), 200, l2);
    serve(l2, false, "面孔二");
    reap(act2);

    std::printf("\n对照:面孔一关停被拖到下一条连接来(t=200 的旗 t≈470 才生效)还多接了一条;\n"
                "面孔二 EINTR 即退,但 handler+flag+每个阻塞点查旗三件套缺一不可——\n"
                "signalfd 版(E4-a)把信号变事件,阻塞点们都不用挨个配对。\n");
    return 0;
}
