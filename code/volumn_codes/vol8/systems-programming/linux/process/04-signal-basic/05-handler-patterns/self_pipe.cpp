// E5 模式二:self-pipe trick——handler 只 write 一个字节进管道,主循环 poll 管道读端,
//        信号与其他 fd 的事件在同一个事件循环里统一调度(ch04 select/poll/epoll 的钩子)
// 编译:g++ -std=c++20 -Wall -Wextra -O2 self_pipe.cpp -o self_pipe && ./self_pipe
//
// 为什么这样是对的:
//   1. write(2) 在异步信号安全表内——handler 里唯一在做的"事"就是它。
//   2. 管道是内核缓冲:字节排队不丢(除非塞满),主循环什么时候来取都行。
//   3. 主循环里 poll/读管道、格式化、打日志,全是普通上下文,没有任何 handler 约束。
// O_NONBLOCK 的两处意义:handler 的 write 在管道满时立刻返回 EAGAIN 而不是永久阻塞
// (handler 里阻塞 = 整个程序卡死)。主循环 drain 时 read 读到 EAGAIN 知道读干净了。
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

int g_pipe_w = -1; // handler 只依赖这一个全局(fd 是小整数,读它本身安全)

void on_signal(int sig) {
    char c = (sig == SIGINT) ? 'i' : (sig == SIGTERM) ? 't' : '?';
    write(g_pipe_w, &c, 1); // 忽略返回值:EAGAIN(管道满)时这一个字节被丢弃
}

long now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void msleep(long ms) {
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const long t0 = now_ms();

    int fds[2];
    if (pipe2(fds, O_NONBLOCK | O_CLOEXEC) != 0) {
        perror("pipe2");
        return 1;
    }
    g_pipe_w = fds[1];

    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // handler 里没有慢速调用可被打断,EINTR 话题不涉及
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    // 编排:子进程在 100/500/550ms 发 SIGINT、SIGINT、SIGTERM
    // (500ms 前那 400ms 空档让 poll 超时心跳也出场一次)
    pid_t pid = fork();
    if (pid == 0) {
        msleep(100);
        kill(getppid(), SIGINT);
        msleep(400);
        kill(getppid(), SIGINT);
        msleep(50);
        kill(getppid(), SIGTERM);
        _exit(0);
    }

    std::printf("t=%4ldms event loop starts, poll(pipe_r, 200ms timeout)\n", now_ms() - t0);
    struct pollfd pfd{fds[0], POLLIN, 0};
    bool running = true;
    while (running) {
        int r = poll(&pfd, 1, 200);
        if (r == -1) {
            if (errno == EINTR)
                continue; // poll 也可能被信号打断,再来一次
            perror("poll");
            break;
        }
        if (r == 0) { // 超时:事件循环的"心跳"槽位——真实程序里这里做周期任务
            std::printf("t=%4ldms (heartbeat, nothing readable)\n", now_ms() - t0);
            continue;
        }
        if (pfd.revents & POLLIN) {
            char buf[64];
            for (;;) { // drain:一次可读事件把管道里的字节全取走
                ssize_t n = read(fds[0], buf, sizeof buf);
                if (n == -1 && errno == EAGAIN)
                    break; // 非阻塞读干净了
                if (n <= 0)
                    break;
                for (ssize_t k = 0; k < n; ++k) {
                    char c = buf[k];
                    if (c == 'i')
                        std::printf("t=%4ldms poll woke up -> byte 'i' = SIGINT "
                                    "(graceful handling)\n",
                                    now_ms() - t0);
                    else if (c == 't') {
                        std::printf("t=%4ldms poll woke up -> byte 't' = SIGTERM "
                                    "-> shutdown\n",
                                    now_ms() - t0);
                        running = false;
                    }
                }
            }
        }
    }
    std::printf("t=%4ldms event loop exited cleanly\n", now_ms() - t0);
    close(fds[0]);
    close(fds[1]);
    int st = 0;
    waitpid(pid, &st, 0);
    return 0;
}
