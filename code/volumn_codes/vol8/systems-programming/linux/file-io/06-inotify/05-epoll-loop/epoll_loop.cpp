// epoll_loop.cpp —— inotify 上 epoll(文章《inotify 文件监控》E5)
//
// inotify 实例就是一个 fd,「有事件可读」= 读端就绪 —— 和 pipe、timerfd
// 没有任何特权差别,塞进同一个 epoll(默认水平触发)统一调度:
//
//   epoll_wait 一次返回的 events[] 里,三类 fd 混着来:
//     pipe     ← 子进程每 300ms 发一行消息
//     timerfd  ← 每 500ms 到期
//     inotify  ← 子进程每 900ms 对测试目录动一次手
//
// 一个循环吃三家,这就是「一切皆 fd」落在文件监控上的样子,也是本篇与
// ch04 多路复用的衔接点。
#include "article.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

const std::string root = "/home/charliechen/l06_scratch/e5/tree";
constexpr std::uint32_t watch_mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE |
                                     IN_MOVED_FROM |
                                     IN_MOVED_TO; // E5 只要主干事件,OPEN/ACCESS 噪声不要

const auto t0 = std::chrono::steady_clock::now();

long ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 t0)
        .count();
}

// 子进程:消息管道上每 300ms 发一行,每 900ms 动一次目录,约 3.2s 后收工
int child_work(int msgfd) {
    for (int round = 1; round <= 3; ++round) {
        for (int tick = 1; tick <= 3; ++tick) {
            ::usleep(300000);
            std::string line = "tick " + std::to_string(round) + "-" + std::to_string(tick) + "\n";
            if (::write(msgfd, line.c_str(), line.size()) == static_cast<ssize_t>(line.size())) {
                // 管道写成功
            }
        }
        ::usleep(200000); // 对齐到 ~900ms 一动的节奏
        const std::string f = root + "/note.txt";
        switch (round) {
            case 1: { // create + write + close
                int fd = ::open(f.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
                if (fd != -1) {
                    ::write(fd, "v1", 2);
                    ::close(fd);
                }
                break;
            }
            case 2: { // modify + close
                int fd = ::open(f.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC);
                if (fd != -1) {
                    ::write(fd, "-v2", 3);
                    ::close(fd);
                }
                break;
            }
            case 3: { // rename:一对 cookie
                ::rename(f.c_str(), (root + "/note2.txt").c_str());
                break;
            }
            default:
                break;
        }
    }
    ::usleep(300000);
    std::string bye = "BYE\n";
    ::write(msgfd, bye.c_str(), bye.size());
    return 0;
}

} // namespace

int main() {
    std::printf("E5 inotify × epoll —— root: %s\n", root.c_str());
    print_inotify_limits();
    std::string rm = "rm -rf '" + root + "'";
    if (::system(rm.c_str()) != 0) {
        return 1;
    }
    if (::mkdir("/home/charliechen/l06_scratch/e5", 0755) == -1 && errno != EEXIST) {
        std::perror("mkdir");
        return 1;
    }
    if (::mkdir(root.c_str(), 0755) == -1) {
        std::perror("mkdir");
        return 1;
    }

    // ---- 三个 fd:inotify / timerfd / pipe ----
    unique_fd ifd{sys_call("inotify_init1", ::inotify_init1, IN_NONBLOCK | O_CLOEXEC)};
    const int wd = static_cast<int>(
        sys_call("inotify_add_watch", ::inotify_add_watch, ifd.get(), root.c_str(), watch_mask));

    unique_fd tfd{
        sys_call("timerfd_create", ::timerfd_create, CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)};
    itimerspec spec{};
    spec.it_interval.tv_sec = 0;
    spec.it_interval.tv_nsec = 500000000; // 500ms 周期
    spec.it_value = spec.it_interval;
    sys_call("timerfd_settime", ::timerfd_settime, tfd.get(), 0, &spec, nullptr);

    int msgfds[2];
    if (::pipe(msgfds) == -1) {
        std::perror("pipe");
        return 1;
    }
    unique_fd msg_r{msgfds[0]};

    std::fflush(stdout);
    pid_t pid = ::fork();
    if (pid == 0) {
        unique_fd msg_w{msgfds[1]};
        ::close(msgfds[0]);
        _exit(child_work(msg_w.get()));
    }
    // 父进程不需要写端,直接关:子进程成为唯一写端,BYE 消息即收工信号
    ::close(msgfds[1]);

    // ---- epoll:三家一起挂,水平触发 ----
    unique_fd ep{sys_call("epoll_create1", ::epoll_create1, EPOLL_CLOEXEC)};

    auto add = [&](int fd, const char* tag) {
        epoll_event ev{};
        ev.events = EPOLLIN; // 默认水平触发:只要还有的读,每次 epoll_wait 都报
        ev.data.fd = fd;
        sys_call("epoll_ctl", ::epoll_ctl, ep.get(), EPOLL_CTL_ADD, fd, &ev);
        std::printf("epoll_ctl(ADD) fd=%d (%s), events=EPOLLIN(水平触发)\n", fd, tag);
    };
    add(ifd.get(), "inotify");
    add(tfd.get(), "timerfd ");
    add(msg_r.get(), "pipe     ");
    std::printf("wd=%d → %s\n\n", wd, root.c_str());

    event_reader reader;
    bool bye = false;
    while (!bye) {
        epoll_event out[8];
        int n = static_cast<int>(sys_call("epoll_wait", ::epoll_wait, ep.get(), out, 8, 5000));
        for (int i = 0; i < n && !bye; ++i) {
            const int fd = out[i].data.fd;
            if (fd == msg_r.get()) {
                char buf[128];
                ssize_t r = ::read(fd, buf, sizeof buf - 1);
                if (r > 0) {
                    buf[r] = '\0';
                    for (char* line = std::strtok(buf, "\n"); line != nullptr;
                         line = std::strtok(nullptr, "\n")) {
                        std::printf("[%5ld ms] pipe    :\"%s\"\n", ms(), line);
                        if (std::string(line) == "BYE") {
                            bye = true;
                        }
                    }
                }
            } else if (fd == tfd.get()) {
                std::uint64_t expirations = 0;
                ::read(fd, &expirations, sizeof expirations);
                std::printf("[%5ld ms] timerfd :到期 %lu 次\n", ms(),
                            static_cast<unsigned long>(expirations));
            } else if (fd == ifd.get()) {
                for (const auto& e : reader.drain(fd)) {
                    std::printf("[%5ld ms] inotify :%s\n", ms(), format_event(e).c_str());
                }
            }
        }
    }

    // 收尾:排干残余
    ::usleep(200000);
    for (const auto& e : reader.drain(ifd.get())) {
        std::printf("[%5ld ms] inotify :%s\n", ms(), format_event(e).c_str());
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    std::printf("\n子进程退出 status=0x%x —— 一个 epoll_wait 循环,"
                "三类 fd 统一调度收线\n",
                status);
    return 0;
}
