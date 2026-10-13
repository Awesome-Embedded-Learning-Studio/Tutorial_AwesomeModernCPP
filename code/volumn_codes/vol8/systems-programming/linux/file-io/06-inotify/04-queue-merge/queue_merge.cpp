// queue_merge.cpp —— 事件队列:合并与溢出(文章《inotify 文件监控》E4)
//
// 事件流不是「一次操作一条」的账本,这组实验把两件最容易被传歪的事钉死:
//
//   A) 读者跟上:写 N 次收 N 条 IN_MODIFY,零合并
//   B) 读者不读 + 背靠背写「同一个」文件 200 次:收到的远少于 200 ——
//      内核在入队时把「wd/mask/cookie/name 四者全同且紧挨着」的事件
//      合并成一条(计数意义丢失,这是按事件数统计写入次数必错的根源)
//   C) 读者不读 + 轮流写 4 个不同名字的文件 200 次:200 条一条不少 ——
//      合并只叠「完全相同且相邻」的,名字不同就不算同一条
//   D) 灌爆:4 个子进程各轮写 128 个文件 1.5 秒,父进程全程不读 →
//      队列过 max_queued_events,读出 wd=-1 的 IN_Q_OVERFLOW
//
// 目录布局:A/B 的 same.txt 放在不加目录 watch 的 solo/ 下(只挂文件级
// watch),C/D 用 root 的目录级 watch —— 两只表互不串线。
#include "article.hpp"

#include <chrono>
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

const std::string root = "/home/charliechen/l06_scratch/e4";
constexpr int kRoundTrips = 10;   // A 组:一写一读跑几轮
constexpr int kBurstWrites = 200; // B/C 组:背靠背写多少次
constexpr int kWriters = 4;       // D 组:并发写者数
constexpr int kFilesPerWriter = 128;
constexpr int kFloodMs = 1500; // D 组:灌多久

void make_root() {
    std::string rm = "rm -rf '" + root + "'";
    if (::system(rm.c_str()) != 0) {
        ::exit(1);
    }
    if (::mkdir(root.c_str(), 0755) == -1 || ::mkdir((root + "/solo").c_str(), 0755) == -1) {
        std::perror("mkdir");
        ::exit(1);
    }
}

// A/B/C 组共用的单写者子进程:
//   cmd=1:写一拍(给 A 组,写完等下一令)
//   cmd=2:对 solo/same.txt 背靠背写 kBurstWrites 次(B 组)
//   cmd=3:轮流写 root/c0..c3 共 kBurstWrites 次(C 组)
void child_one_shot(int cmdfd, int ackfd) {
    int same =
        ::open((root + "/solo/same.txt").c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (same == -1) {
        _exit(10);
    }
    int multi[4] = {-1, -1, -1, -1};
    for (int i = 0; i < 4; ++i) {
        multi[i] = ::open((root + "/c" + std::to_string(i) + ".txt").c_str(),
                          O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (multi[i] == -1) {
            _exit(11);
        }
    }
    for (;;) {
        unsigned char cmd = 0;
        ssize_t r = ::read(cmdfd, &cmd, 1);
        if (r == -1 && errno == EINTR) {
            continue;
        }
        if (r <= 0) {
            _exit(0);
        }
        if (cmd == 1) {
            ::write(same, "x", 1);
        } else if (cmd == 2) {
            for (int i = 0; i < kBurstWrites; ++i) {
                ::write(same, "x", 1);
            }
        } else if (cmd == 3) {
            for (int i = 0; i < kBurstWrites; ++i) {
                ::write(multi[i % 4], "x", 1);
            }
        }
        if (::write(ackfd, &cmd, 1) != 1) {
            _exit(1);
        }
    }
}

// D 组灌队列的写者:备好 128 个文件,等放行后狂写到时限
int flood_writer(int releasefd, int slot) {
    std::vector<int> fds;
    for (int i = 0; i < kFilesPerWriter; ++i) {
        std::string p = root + "/w" + std::to_string(slot) + "_" + std::to_string(i) + ".bin";
        int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (fd == -1) {
            return 10;
        }
        fds.push_back(fd);
    }
    for (;;) {
        unsigned char go = 0;
        ssize_t r = ::read(releasefd, &go, 1);
        if (r == -1 && errno == EINTR) {
            continue;
        }
        if (r != 1) {
            return 11;
        }
        break;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kFloodMs);
    long wrote = 0;
    int i = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        for (int burst = 0; burst < 256; ++burst) {
            wrote += (::write(fds[i % kFilesPerWriter], "x", 1) == 1);
            ++i;
        }
    }
    std::printf("  [写者 %d] 完成写次数 %ld\n", slot, wrote);
    std::fflush(stdout); // _exit 不冲 stdio 缓冲,这里手动冲
    return 0;
}

} // namespace

int main() {
    std::printf("E4 队列:合并与溢出 —— root: %s\n", root.c_str());
    print_inotify_limits();
    make_root();

    unique_fd ifd{sys_call("inotify_init1", ::inotify_init1, IN_NONBLOCK | O_CLOEXEC)};
    event_reader reader;

    // ---- A:读者跟上,零合并 ----
    // same.txt 得先存在(watch 挂的是 inode,文件还没有就无从挂起)
    {
        int fd = ::open((root + "/solo/same.txt").c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
        if (fd == -1) {
            std::perror("open same.txt");
            return 1;
        }
        ::close(fd);
    }
    const int wd_same = static_cast<int>(sys_call("add_watch(solo/same.txt)", ::inotify_add_watch,
                                                  ifd.get(), (root + "/solo/same.txt").c_str(),
                                                  static_cast<std::uint32_t>(IN_MODIFY)));
    std::printf("wd_same=%d → solo/same.txt(文件级,只报 IN_MODIFY)\n", wd_same);

    int cmdfds[2], ackfds[2];
    if (::pipe(cmdfds) == -1 || ::pipe(ackfds) == -1) {
        std::perror("pipe");
        return 1;
    }
    std::fflush(stdout); // fork 前冲干净:不然子进程带着父缓冲区副本,会把旧账再写一遍
    pid_t pid = ::fork();
    if (pid == 0) {
        ::close(cmdfds[1]);
        ::close(ackfds[0]);
        child_one_shot(cmdfds[0], ackfds[1]);
        _exit(0);
    }
    unique_fd cmd_w{cmdfds[1]}, ack_r{ackfds[0]};
    ::close(cmdfds[0]);
    ::close(ackfds[1]);

    auto send = [&](unsigned char cmd) {
        if (::write(cmd_w.get(), &cmd, 1) != 1) {
            return false;
        }
        for (;;) {
            unsigned char ack = 0;
            ssize_t r = ::read(ack_r.get(), &ack, 1);
            if (r == -1 && errno == EINTR) {
                continue;
            }
            return r == 1 && ack == cmd;
        }
    };

    std::printf("\n==== A 读者跟上:每写一拍读一拍 ====\n");
    long total_a = 0;
    for (int i = 0; i < kRoundTrips; ++i) {
        send(1);
        total_a += static_cast<long>(reader.drain(ifd.get()).size());
    }
    std::printf("%d 次写 → %ld 条 IN_MODIFY:读者跟上,一条不少\n", kRoundTrips, total_a);

    // ---- B:不读 + 同文件背靠背 200 写 ----
    std::printf("\n==== B 读者不读:%d 次背靠背写 solo/same.txt ====\n", kBurstWrites);
    send(2);
    auto events_b = reader.drain(ifd.get());
    std::printf("%d 次写 → %zu 条事件:\n", kBurstWrites, events_b.size());
    for (const auto& e : events_b) {
        std::printf("  %s\n", format_event(e).c_str());
    }
    std::printf("  # 四元组(wd=%d, IN_MODIFY, cookie=0, name='')全同且相邻,"
                "内核入队时叠成一条:事件数 ≠ 写次数,IN_MODIFY 不能当计数器\n",
                wd_same);

    // ---- C:不读 + 4 个文件轮流 200 写(root 挂目录级 watch)----
    const int wd_root =
        static_cast<int>(sys_call("add_watch(root)", ::inotify_add_watch, ifd.get(), root.c_str(),
                                  static_cast<std::uint32_t>(IN_MODIFY | IN_CREATE)));
    std::printf("\nwd_root=%d → root/(目录级,报 IN_MODIFY|IN_CREATE)\n", wd_root);

    std::printf("==== C 读者不读:轮流写 c0..c3 共 %d 次 ====\n", kBurstWrites);
    send(3);
    auto events_c = reader.drain(ifd.get());
    std::printf("%d 次写 → %zu 条事件(首尾各 3):\n", kBurstWrites, events_c.size());
    for (std::size_t i = 0; i < events_c.size() && i < 3; ++i) {
        std::printf("  %s\n", format_event(events_c[i]).c_str());
    }
    std::printf("  ...\n");
    for (std::size_t i = events_c.size() < 3 ? 0 : events_c.size() - 3; i < events_c.size(); ++i) {
        std::printf("  %s\n", format_event(events_c[i]).c_str());
    }
    std::printf("  # 名字轮着来,队尾永远和来客不同 → 一条不合并:"
                "合并条件是「完全相同且相邻」,不是「同类事件都算一条」\n");

    // ---- D:灌爆队列 ----
    const auto lim = read_inotify_limits();
    std::printf("\n==== D 灌爆队列:%d 写者 × %d 文件 × %d ms,父进程全程不读 ====\n", kWriters,
                kFilesPerWriter, kFloodMs);
    std::printf("max_queued_events = %ld\n", lim.max_queued_events);

    cmd_w.reset(); // 单写者收工
    int status = 0;
    ::waitpid(pid, &status, 0);

    // 放行管道:写者先各自备好文件,再统一开闸
    int relfds[2];
    if (::pipe(relfds) == -1) {
        std::perror("pipe");
        return 1;
    }
    std::vector<pid_t> kids;
    for (int k = 0; k < kWriters; ++k) {
        std::fflush(stdout); // 同上:fork 前必须冲
        pid_t w = ::fork();
        if (w == 0) {
            ::close(relfds[1]);
            _exit(flood_writer(relfds[0], k));
        }
        kids.push_back(w);
    }
    unique_fd rel_w{relfds[1]};
    ::close(relfds[0]);

    // 写者备文件产生的 IN_CREATE 先读走,不掺和灌队列阶段
    ::usleep(400000);
    auto created = reader.drain(ifd.get());
    std::printf("(备场:%d 个文件创建,IN_CREATE %zu 条,先读走不计数)\n", kWriters * kFilesPerWriter,
                created.size());

    if (::write(rel_w.get(), "gggg", kWriters) != kWriters) {
        std::perror("release");
        return 1;
    }
    // 全程不读,睡过整个灌队列窗口
    ::usleep((kFloodMs + 700) * 1000);
    for (pid_t w : kids) {
        ::waitpid(w, &status, 0);
    }
    rel_w.reset();

    auto events_d = reader.drain(ifd.get());
    long overflow_count = 0;
    long overflow_pos = -1, n = 0;
    for (const auto& e : events_d) {
        if (e.mask & IN_Q_OVERFLOW) {
            if (overflow_pos == -1) {
                overflow_pos = n;
            }
            ++overflow_count;
        }
        ++n;
    }
    std::printf("读出 %zu 条事件,其中 IN_Q_OVERFLOW %ld 条,首次出现在第 %ld 条:\n", events_d.size(),
                overflow_count, overflow_pos);
    for (std::size_t i = 0; i < events_d.size() && i < 2; ++i) {
        std::printf("  %s\n", format_event(events_d[i]).c_str());
    }
    std::printf("  ...\n");
    if (overflow_pos >= 0 && static_cast<std::size_t>(overflow_pos) < events_d.size()) {
        std::printf("  %s\n",
                    format_event(events_d[static_cast<std::size_t>(overflow_pos)]).c_str());
    }
    std::printf("  ...\n");
    for (std::size_t i = events_d.size() < 2 ? 0 : events_d.size() - 2; i < events_d.size(); ++i) {
        std::printf("  %s\n", format_event(events_d[i]).c_str());
    }
    std::printf("  # 溢出条目 wd=-1、mask=IN_Q_OVERFLOW:恰好排在第 %ld 条"
                "(0 起),前面 %ld 条是队列装得下的部分;此后写者仍在写,"
                "但再没有事件进来 —— 溢出后丢了多少没有账可查,"
                "监控程序要么读得快,要么靠应用层对账\n",
                overflow_count > 0 ? events_d.size() - 1 : 0,
                overflow_count > 0 ? events_d.size() - 1 : 0);
    return 0;
}
