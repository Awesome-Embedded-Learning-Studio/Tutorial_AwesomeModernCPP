// recursive_watch.cpp —— 递归监控的亲手实现(文章《inotify 文件监控》E3)
//
// 内核不递归(E2 已证),递归就得自己搭:
//   1) 起手 walk 整棵目录树,每个目录 add_watch(带 IN_ONLYDIR 防呆)
//   2) 事件里见 IN_CREATE|IN_ISDIR → 对新目录补挂 watch
//   3) 补挂要递归补:mkdir -p 一次性造 a/b/c 时,等到事件到手 a/b 早就
//      存在了,只挂 a 会漏 —— add_tree 从新目录往下整棵走一遍
//   4) IN_IGNORED(watch 随目录消亡)→ 从 wd 表里摘掉
//
// 子进程四波动作:嵌套树 → 文件 → 更深的树 → 删除,事件全路径打印。
// 收尾统计 add_watch 次数,与 /proc/sys/fs/inotify/max_user_watches 对账。
#include "article.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

const std::string root = "/home/charliechen/l06_scratch/e3/tree";

// 挂在 watch 上的 mask:目录的生老病死 + 进出目录的文件
constexpr std::uint32_t watch_mask = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO |
                                     IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR |
                                     IN_EXCL_UNLINK;

class recursive_watcher {
  public:
    recursive_watcher(unique_fd& ifd, fs::path root) : ifd_(&ifd) {
        add_tree(std::move(root), true);
    }

    // 对 dir 及其全部后代目录各挂一只 watch(起点自己也要挂:
    // recursive_directory_iterator 遍历的是孩子,不含起点)
    void add_tree(const fs::path& dir, bool announce) {
        add_one(dir, announce);
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(
                 dir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) {
                break;
            }
            if (!it->is_directory(ec)) {
                continue;
            }
            add_one(it->path(), announce);
        }
    }

    void add_one(const fs::path& dir, bool announce) {
        int wd = ::inotify_add_watch(ifd_->get(), dir.c_str(), watch_mask);
        if (wd == -1) {
            // 目录可能刚被删(与删除事件赛跑):如实记账,不算失败
            std::printf("  [watch] add_watch(%s) 失败:%s\n", dir.c_str(), std::strerror(errno));
            return;
        }
        if (wd2path_.count(wd) == 0) {
            adds_++;
        }
        wd2path_[wd] = dir;
        if (announce) {
            std::printf("  [watch] add_watch(wd=%d) %s\n", wd, dir.c_str());
        }
    }

    // 收一轮事件:打印全路径,新目录补挂,亡目录摘牌
    void pump() {
        for (const auto& e : reader_.drain(ifd_->get())) {
            if (e.mask & IN_Q_OVERFLOW) {
                std::printf("  [event] *** IN_Q_OVERFLOW,wd=%d ***\n", e.wd);
                continue;
            }
            if (e.mask & IN_IGNORED) { // watch 随目录消亡,内核已摘,同步摘表
                wd2path_.erase(e.wd);
                std::printf("  [event] wd=%d 目录消亡,IN_IGNORED,摘表(剩 %zu 只)\n", e.wd,
                            wd2path_.size());
                continue;
            }
            fs::path base = wd2path_.count(e.wd) ? wd2path_[e.wd] : fs::path("(已摘的 wd)");
            fs::path full = e.name.empty() ? base : base / e.name;
            std::printf("  [event] %s\n", (std::string("wd=") + std::to_string(e.wd) + "  " +
                                           mask_to_str(e.mask) + "  " + full.string())
                                              .c_str());
            if ((e.mask & IN_CREATE) && (e.mask & IN_ISDIR)) {
                // 竞态兜底:等事件到手,孙目录可能已在,整棵补挂
                add_tree(full, true);
            }
        }
    }

    std::size_t watch_count() const { return wd2path_.size(); }
    std::size_t total_adds() const { return adds_; }

  private:
    unique_fd* ifd_;
    std::unordered_map<int, fs::path> wd2path_;
    event_reader reader_;
    std::size_t adds_ = 0;
};

// ---------------------------------------------------------------- 子进程脚本

void child_step(int cmd) {
    const fs::path p{root};
    std::error_code ec;
    switch (cmd) {
        case 1: // 一次性造三层嵌套( mkdir -p a/b/c )
            fs::create_directories(p / "a/b/c", ec);
            break;
        case 2: // 三层各放一个文件
            for (const char* rel : {"a/1.txt", "a/b/2.txt", "a/b/c/3.txt"}) {
                int fd = ::open((p / rel).c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
                if (fd != -1) {
                    ::close(fd);
                }
            }
            break;
        case 3: // 更深的一棵:deep/x/y
            fs::create_directories(p / "deep/x/y", ec);
            break;
        case 4: // 深处放文件 + 删一个旧文件
        {
            int fd = ::open((p / "deep/x/y/4.txt").c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
            if (fd != -1) {
                ::close(fd);
            }
            ::unlink((p / "a/1.txt").c_str());
            break;
        }
        case 5: // 整棵搬走 deep(目录自身的 MOVE_SELF/IGNORED 不会来:
                // 目录搬家不递归通知,只有 watch 的目录自己动才有)
            fs::rename(p / "deep", p / "deep2", ec);
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
    "mkdir -p a/b/c(嵌套树)",
    "touch a/1.txt a/b/2.txt a/b/c/3.txt",
    "mkdir -p deep/x/y",
    "touch deep/x/y/4.txt + rm a/1.txt",
    "mv deep → deep2(整目录改名)",
};

} // namespace

int main() {
    std::printf("E3 递归监控 —— root: %s\n", root.c_str());
    print_inotify_limits();

    std::string rm = "rm -rf '" + root + "'";
    if (::system(rm.c_str()) != 0) {
        return 1;
    }
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        std::printf("create_directories 失败:%s\n", ec.message().c_str());
        return 1;
    }

    unique_fd ifd{sys_call("inotify_init1", ::inotify_init1, IN_NONBLOCK | O_CLOEXEC)};

    std::printf("\n起手:walk 整棵树,每个目录挂一只 watch\n");
    recursive_watcher watcher{ifd, fs::path{root}};
    std::printf("起手 watch 数:%zu\n", watcher.watch_count());

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
        watcher.pump();
    };

    run_step(1);
    std::printf("  # 事件只报了 a 的 create;a/b、a/b/c 在事件到手前就已被"
                "create_directories 造好,靠 add_tree 的兜底 walk 补挂\n");
    run_step(2);
    run_step(3);
    std::printf("  # 同样的竞态:deep/x、deep/x/y 靠兜底 walk 补挂\n");
    run_step(4);
    run_step(5);
    std::printf("  # 目录整棵改名:父目录(wd=1)报一对 MOVED_FROM/TO;"
                "deep 自己的 wd=5 报 IN_MOVE_SELF;deep/x、deep/x/y 虽然"
                "路径全变了,却一个事件都没有 —— 内核只为被改名的 inode 本身"
                "报 MOVE_SELF,不替后代报。三只 watch 都仍有效(绑 inode),"
                "但 wd 表里的旧路径已过期:真要追路径,得在 MOVE_SELF 时改表\n");

    cmd_w.reset();
    int status = 0;
    ::waitpid(pid, &status, 0);

    const auto lim = read_inotify_limits();
    std::printf("\n==== 对账 ====\n");
    std::printf("存活 watch:%zu 只,累计 add_watch:%zu 次\n", watcher.watch_count(),
                watcher.total_adds());
    std::printf("max_user_watches = %ld,占比 %.4f%% —— 一只手表一个 inode,"
                "深树大树的成本主要在这儿\n",
                lim.max_user_watches,
                100.0 * static_cast<double>(watcher.watch_count()) /
                    static_cast<double>(lim.max_user_watches));
    std::printf("子进程退出 status=0x%x\n", status);
    return 0;
}
