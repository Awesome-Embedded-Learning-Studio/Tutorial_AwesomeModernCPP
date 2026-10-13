// dir_vs_file.cpp —— 目录 vs 文件,与「不递归」实证(文章《inotify 文件监控》E2)
//
// 四组对照:
//   A) 只 watch 目录 A:子目录 B 里新建文件 → 0 个事件(inotify 不递归,
//      想要子目录就得自己对每个目录 add_watch)
//   B) 给 B 也 add_watch 后:同一动作在 B 的 wd 上报出来;
//      顺手演示 IN_ONLYDIR(对文件加 watch 带它 → ENOTDIR)
//   C) watch 单个文件:事件照来但 name 恒空(len=0);
//      文件改名 → IN_MOVE_SELF(watch 跟着 inode 走,不跟路径);
//      原路径换上新 inode 后再写 → 文件 watch 哑了(绑的是旧 inode)
//   D) 被 watch 的文件被删 → IN_ATTRIB + IN_DELETE_SELF + IN_IGNORED,
//      watch 随之失效(再 rm_watch = EINVAL)
//
// 同步口径与 E1 相同:父进程管道发令,子进程做完回 ack,事件在写方
// syscall 返回前已入队,ack 到了就能读。
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

const std::string root = "/home/charliechen/l06_scratch/e2";

void child_step(int cmd) {
    const std::string a = root + "/A";
    const std::string f = a + "/f";
    const std::string fbak = a + "/f.bak";
    switch (cmd) {
        case 1: { // touch A/x(短名)+ 一个长名文件(看 len 填充)
            int fd = ::open((a + "/x").c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
            if (fd == -1) {
                _exit(10);
            }
            ::close(fd);
            break;
        }
        case 2: // mkdir A/B
            if (::mkdir((a + "/B").c_str(), 0755) == -1) {
                _exit(11);
            }
            break;
        case 3: // touch A/B/y —— 关键一步:不递归的话这里什么都不该报
        {
            int fd = ::open((a + "/B/y").c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
            if (fd == -1) {
                _exit(12);
            }
            ::close(fd);
            break;
        }
        case 4: // 长文件名:len 的 16 字节对齐填充看得最清楚
        {
            int fd = ::open((a + "/a-very-long-filename-example.txt").c_str(),
                            O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
            if (fd == -1) {
                _exit(13);
            }
            ::close(fd);
            break;
        }
        case 5: // 创建 A/f 并写入(给 C 组当被 watch 的文件)
        {
            int fd = ::open(f.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            if (fd == -1) {
                _exit(14);
            }
            if (::write(fd, "data", 4) != 4) {
                _exit(15);
            }
            ::close(fd);
            break;
        }
        case 6: // open/write/close A/f:文件 watch 应报 OPEN/MODIFY/CLOSE_WRITE
        {
            int fd = ::open(f.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC);
            if (fd == -1) {
                _exit(16);
            }
            if (::write(fd, "more", 4) != 4) {
                _exit(17);
            }
            ::close(fd);
            break;
        }
        case 7: // rename A/f → A/f.bak:目录 watch 报 FROM/TO,文件 watch 报 MOVE_SELF
            if (::rename(f.c_str(), fbak.c_str()) == -1) {
                _exit(18);
            }
            break;
        case 8: // 原路径放一个新 inode 再写:文件 watch(旧 inode)应保持沉默
        {
            int fd = ::open(f.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            if (fd == -1) {
                _exit(19);
            }
            if (::write(fd, "new", 3) != 3) {
                _exit(20);
            }
            ::close(fd);
            break;
        }
        case 9: // unlink A/f.bak:被 watch 的旧 inode 走到头
            if (::unlink(fbak.c_str()) == -1) {
                _exit(21);
            }
            break;
        default:
            _exit(99);
    }
}

void child_loop(int cmdfd, int ackfd) {
    for (;;) {
        unsigned char cmd = 0;
        ssize_t r = ::read(cmdfd, &cmd, 1);
        if (r == -1 && errno == EINTR) {
            continue;
        }
        if (r <= 0) {
            _exit(0);
        }
        child_step(cmd);
        if (::write(ackfd, &cmd, 1) != 1) {
            _exit(1);
        }
    }
}

const char* const step_desc[] = {
    "",
    "touch A/x",
    "mkdir A/B",
    "touch A/B/y(watch 只挂在 A 上)",
    "touch A/a-very-long-filename-example.txt",
    "创建并写入 A/f(给文件 watch 用)",
    "open+write+close A/f",
    "rename A/f → A/f.bak",
    "原路径 A/f 放上新 inode 并写入",
    "unlink A/f.bak(被 watch 的旧 inode 被删)",
};

} // namespace

int main() {
    std::printf("E2 目录 vs 文件 —— root: %s\n", root.c_str());
    print_inotify_limits();

    std::string rm = "rm -rf '" + root + "'";
    if (::system(rm.c_str()) != 0) {
        return 1;
    }
    if (::mkdir(root.c_str(), 0755) == -1 || ::mkdir((root + "/A").c_str(), 0755) == -1) {
        std::perror("mkdir");
        return 1;
    }

    unique_fd ifd{sys_call("inotify_init1", ::inotify_init1, IN_NONBLOCK | O_CLOEXEC)};
    event_reader reader;

    // ---- A 组:只挂 A
    const int wd_a = static_cast<int>(sys_call("inotify_add_watch(A)", ::inotify_add_watch,
                                               ifd.get(), (root + "/A").c_str(),
                                               static_cast<std::uint32_t>(IN_ALL_EVENTS)));
    std::printf("wd_a=%d → A/(目录)\n", wd_a);

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
        std::printf("\n==== 第 %d 步:%s ====\n", step, step_desc[step]);
        unsigned char c = static_cast<unsigned char>(step);
        if (::write(cmd_w.get(), &c, 1) != 1) {
            return;
        }
        for (;;) {
            unsigned char ack = 0;
            ssize_t r = ::read(ack_r.get(), &ack, 1);
            if (r == -1 && errno == EINTR) {
                continue;
            }
            if (r != 1) {
                return;
            }
            break;
        }
        auto events = reader.drain(ifd.get());
        if (events.empty()) {
            std::printf("  (0 个事件)\n");
        }
        for (const auto& e : events) {
            std::printf("  %s\n", format_event(e).c_str());
        }
    };

    run_step(1);
    run_step(2);
    run_step(3);
    std::printf("  # ★ inotify 不递归:watch 的是 A,孙子辈 A/B/y 的 create"
                "一个事件都没报\n");
    run_step(4);
    std::printf("  # 长 32 字符的名字:len = 48(32+'\\0' = 33,向上取整到 16 的"
                "倍数)\n");

    // ---- B 组:给 B 也挂上 watch
    const int wd_b = static_cast<int>(sys_call("inotify_add_watch(A/B)", ::inotify_add_watch,
                                               ifd.get(), (root + "/A/B").c_str(),
                                               static_cast<std::uint32_t>(IN_ALL_EVENTS)));
    std::printf("\nwd_b=%d → A/B/(目录) —— 现在子目录也有自己的表了\n", wd_b);
    run_step(3); // 再 touch A/B/y 一次:这回该在 wd_b 上报
    std::printf("  # 同一个动作,A 的 wd_a 沉默、B 的 wd_b 报告:"
                "事件归属按 watch 的目录算,不按路径前缀\n");

    // IN_ONLYDIR:对「文件」加 watch 时带上它,直接 EINVAL
    run_step(5); // 先造出 A/f
    int rc = ::inotify_add_watch(ifd.get(), (root + "/A/x").c_str(),
                                 static_cast<std::uint32_t>(IN_ALL_EVENTS | IN_ONLYDIR));
    std::printf("\nIN_ONLYDIR 对文件 add_watch(A/x) = %d, errno = %d (%s)"
                " —— 只想盯目录时的防呆位\n",
                rc, errno, std::strerror(errno));

    // ---- C 组:watch 单个文件
    const int wd_f = static_cast<int>(sys_call("inotify_add_watch(A/f)", ::inotify_add_watch,
                                               ifd.get(), (root + "/A/f").c_str(),
                                               static_cast<std::uint32_t>(IN_ALL_EVENTS)));
    std::printf("\nwd_f=%d → A/f(单个文件)\n", wd_f);
    std::printf("wd 表:wd_a=%d → A/,wd_b=%d → A/B/,wd_f=%d → A/f\n", wd_a, wd_b, wd_f);

    run_step(6); // open/write/close A/f
    std::printf("  # 文件级 watch 的 name 恒空(len=0):名字是目录 watch 用来"
                "报「孩子是谁」的,自己就是主角,不用报名\n");
    run_step(7); // rename f → f.bak
    std::printf("  # 同一次 rename:wd_a 报 IN_MOVED_FROM/IN_MOVED_TO(cookie 配对),"
                "wd_f 报 IN_MOVE_SELF —— watch 跟着 inode 走,搬家不撒手\n");
    run_step(8); // 原路径换新 inode
    std::printf("  # ★ wd_f 对新 inode 的写入完全沉默:watch 绑 inode 不绑路径,"
                "原地换文件 = 监控脱靶\n");

    // ---- D 组:被 watch 的文件被删
    run_step(9);
    std::printf("  # 旧 inode 被 unlink:IN_ATTRIB(链接数归零)→ IN_DELETE_SELF →"
                " IN_IGNORED,watch 寿终正寝\n");
    rc = ::inotify_rm_watch(ifd.get(), wd_f);
    std::printf("\n之后再 inotify_rm_watch(wd_f=%d) = %d, errno = %d (%s)"
                " —— 随 IN_IGNORED 已被内核摘掉\n",
                wd_f, rc, errno, std::strerror(errno));

    cmd_w.reset();
    int status = 0;
    ::waitpid(pid, &status, 0);
    std::printf("\n子进程退出 status=0x%x\n", status);
    return 0;
}
