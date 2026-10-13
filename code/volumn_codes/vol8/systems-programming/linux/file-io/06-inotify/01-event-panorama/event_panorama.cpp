// event_panorama.cpp —— 事件全景(文章《inotify 文件监控》E1)
//
// 一棵测试目录,子进程按脚本逐步制造一轮常见事件,父进程收一步打一步:
//   create/open/modify/close_write/close_nowrite/access/attrib/
//   moved_from+moved_to(同一次 mv,cookie 配对)/delete/
//   子目录 create|ISDIR / delete|ISDIR / watched 目录自身被删(delete_self+ignored)
//
// 顺带解读四件事:
//   wd     —— add_watch 的返回值,事件靠它指回「哪只手表」
//   mask   —— 多事件位可以挤在同一字段里(如 IN_CREATE|IN_ISDIR)
//   cookie —— 只在搬移对(moved_from/moved_to)里非零,其余事件恒 0
//   len    —— 名字含 '\0' 再向 16 字节对齐填充后的长度,不是 strlen
//
// 同步口径:父进程经管道发命令字节,子进程做完该步回一个 ack 字节。
// 事件是写方 syscall 在返回前同步入队的,所以 ack 到达时事件必已在队列里,
// 不需要 sleep 兜底。
#include "article.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

const std::string root = "/home/charliechen/l06_scratch/e1/tree";
const char* const cmd_step_desc[] = {
    "",                                      // 0 不用
    "open(f.txt, O_WRONLY|O_CREAT|O_TRUNC)", // 1
    "write(fd, \"hello\", 5)",               // 2
    "close(fd)",                             // 3
    "open O_RDONLY + read 2 字节 + close",   // 4
    "chmod(f.txt, 0600)",                    // 5
    "rename(f.txt, g.txt)  同目录改名",      // 6
    "unlink(g.txt)",                         // 7
    "mkdir(sub)",                            // 8
    "rmdir(sub)",                            // 9
    "rmdir(tree)  被监控目录本身被删",       // 10
};

// ---------------------------------------------------------------- 子进程侧

void child_step(int cmd) {
    static int fd = -1; // 第 1 步打开,第 3 步关
    const std::string f = root + "/f.txt";
    const std::string g = root + "/g.txt";
    const std::string sub = root + "/sub";
    switch (cmd) {
        case 1:
            fd = ::open(f.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            if (fd == -1) {
                _exit(10);
            }
            break;
        case 2:
            if (::write(fd, "hello", 5) != 5) {
                _exit(11);
            }
            break;
        case 3:
            ::close(fd);
            fd = -1;
            break;
        case 4: {
            int r = ::open(f.c_str(), O_RDONLY | O_CLOEXEC);
            if (r == -1) {
                _exit(12);
            }
            char two[2];
            if (::read(r, two, 2) != 2) {
                _exit(13);
            }
            ::close(r);
            break;
        }
        case 5:
            if (::chmod(f.c_str(), 0600) == -1) {
                _exit(14);
            }
            break;
        case 6:
            if (::rename(f.c_str(), g.c_str()) == -1) {
                _exit(15);
            }
            break;
        case 7:
            if (::unlink(g.c_str()) == -1) {
                _exit(16);
            }
            break;
        case 8:
            if (::mkdir(sub.c_str(), 0755) == -1) {
                _exit(17);
            }
            break;
        case 9:
            if (::rmdir(sub.c_str()) == -1) {
                _exit(18);
            }
            break;
        case 10:
            if (::rmdir(root.c_str()) == -1) {
                _exit(19);
            }
            break;
        default:
            _exit(20);
    }
}

void child_loop(int cmdfd, int ackfd) {
    for (;;) {
        unsigned char cmd = 0;
        ssize_t r = ::read(cmdfd, &cmd, 1);
        if (r == -1 && errno == EINTR) {
            continue;
        }
        if (r <= 0) { // 父进程关管道 = 收工
            _exit(0);
        }
        child_step(cmd);
        if (::write(ackfd, &cmd, 1) != 1) {
            _exit(1);
        }
    }
}

// ---------------------------------------------------------------- 父进程侧

void banner(int step) {
    std::printf("\n==== 第 %2d 步:%s ====\n", step, cmd_step_desc[step]);
}

void print_events(const std::vector<decoded_event>& events) {
    if (events.empty()) {
        std::printf("  (0 个事件)\n");
    }
    for (const auto& e : events) {
        std::printf("  %s\n", format_event(e).c_str());
    }
}

} // namespace

int main() {
    std::printf("E1 事件全景 —— watched: %s  mask: IN_ALL_EVENTS\n", root.c_str());
    print_inotify_limits();
    std::printf("sizeof(struct inotify_event) = %zu(1 int + 3 uint32 + char name[1],"
                "按 8 字节对齐)\n",
                sizeof(struct inotify_event));

    // 干净的测试目录
    std::string rm = "rm -rf '" + root + "'";
    if (::system(rm.c_str()) != 0) {
        return 1;
    }
    if (::mkdir(root.c_str(), 0755) == -1 && errno != EEXIST) {
        return 1;
    }

    // IN_NONBLOCK:父进程用 drain「现在有什么读什么」;同步由管道握手保证
    unique_fd ifd{sys_call("inotify_init1", ::inotify_init1, IN_NONBLOCK | O_CLOEXEC)};
    event_reader reader;

    const int wd =
        static_cast<int>(sys_call("inotify_add_watch", ::inotify_add_watch, ifd.get(), root.c_str(),
                                  static_cast<std::uint32_t>(IN_ALL_EVENTS)));
    std::printf("inotify_add_watch(\"%s\", IN_ALL_EVENTS) = wd %d\n\n", root.c_str(), wd);

    // 握手管道
    int cmdfds[2], ackfds[2];
    if (::pipe(cmdfds) == -1 || ::pipe(ackfds) == -1) {
        std::perror("pipe");
        return 1;
    }
    pid_t pid = ::fork();
    if (pid == 0) {
        ::close(cmdfds[1]);
        ::close(ackfds[0]);
        child_loop(cmdfds[0], ackfds[1]);
        _exit(0);
    }
    unique_fd cmd_w{cmdfds[1]}, ack_r{ackfds[0]};
    ::close(cmdfds[0]);
    ::close(ackfds[1]);

    auto run_step = [&](int step) {
        banner(step);
        unsigned char c = static_cast<unsigned char>(step);
        if (::write(cmd_w.get(), &c, 1) != 1) {
            return false;
        }
        unsigned char ack = 0;
        for (;;) {
            ssize_t r = ::read(ack_r.get(), &ack, 1);
            if (r == -1 && errno == EINTR) {
                continue;
            }
            if (r != 1) {
                return false;
            }
            break;
        }
        print_events(reader.drain(ifd.get()));
        return true;
    };

    run_step(1); // create + open
    std::printf("  # len=16 不是 strlen(\"f.txt\")=5:是 5+'\\0' 凑 6,再向"
                "16(sizeof(struct inotify_event))对齐填充\n");
    run_step(2); // modify
    run_step(3); // close_write
    run_step(4); // open/access/close_nowrite
    run_step(5); // attrib
    run_step(6); // moved_from + moved_to
    std::printf("  # 同一次 rename 的两条事件 cookie 相同(配对暗号),wd 相同"
                "(没出目录);其余事件 cookie 恒为 0\n");
    run_step(7);  // delete
    run_step(8);  // mkdir:IN_CREATE 里挤着状态位 IN_ISDIR
    run_step(9);  // rmdir
    run_step(10); // 目录自身被删:delete_self + ignored

    // watch 已被内核自动摘除:再 rm_watch 会被 EINVAL 拒绝
    int rc = ::inotify_rm_watch(ifd.get(), wd);
    std::printf("\n目录没了之后再 inotify_rm_watch(wd=%d) = %d, errno = %d (%s)"
                " —— watch 已被内核随 IN_IGNORED 自动摘除\n",
                wd, rc, errno, std::strerror(errno));

    // 收工:关掉命令管道写端,子进程的 read 返回 0 自行退出
    // (不关就会死锁:父在 waitpid,子在等下一条命令)
    cmd_w.reset();

    int status = 0;
    ::waitpid(pid, &status, 0);
    std::printf("\n子进程退出 status=0x%x,事件全景收线\n", status);
    return 0;
}
