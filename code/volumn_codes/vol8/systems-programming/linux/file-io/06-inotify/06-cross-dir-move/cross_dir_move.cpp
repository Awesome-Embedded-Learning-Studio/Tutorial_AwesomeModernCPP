// cross_dir_move.cpp —— 搬移检测:cookie 配对与它的失效边界(文章 E6)
//
// 监控两个目录 A(ext4)与 B,同一次 mv 跨目录:
//   A 侧报 IN_MOVED_FROM(wd_A, cookie=C, name=f)
//   B 侧报 IN_MOVED_TO(wd_B, cookie=C, name=f)
//   两个 wd 不同、cookie 相同 —— 「同一次搬移在两侧各报一次」,
//   cookie 就是把两条事件缝回一次 rename 的线。
//
// 对照组:跨文件系统搬 mv(A 在 ext4,C 在 /dev/shm 的 tmpfs)。
// rename(2) 对跨设备直接 EXDEV 拒绝,mv 只能退化为「复制 + 删除」:
//   源侧看到 OPEN/ACCESS/CLOSE_NOWRITE/DELETE(没有 MOVED_FROM)
//   目的侧看到 CREATE/MODIFY/CLOSE_WRITE/ATTRIB(没有 MOVED_TO)
//   全程 cookie=0 —— cookie 配对只在同文件系统的 rename 里存在,
//   跨设备的「搬移」在事件流里根本不是搬移。
#include "article.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

const std::string dir_a = "/home/charliechen/l06_scratch/e6/A"; // ext4
const std::string dir_b = "/home/charliechen/l06_scratch/e6/B"; // ext4(同设备)
const std::string dir_c = "/dev/shm/l06_e6_C";                  // tmpfs(跨设备)

void drain_print(int ifd, event_reader& reader, const char* expect_note) {
    auto events = reader.drain(ifd);
    if (events.empty()) {
        std::printf("  (0 个事件)%s\n", expect_note);
    }
    for (const auto& e : events) {
        std::printf("  %s\n", format_event(e).c_str());
    }
}

const char* fs_name(long f_type) {
    if (f_type == 0xEF53) {
        return "ext2/3/4";
    }
    if (f_type == 0x01021994) {
        return "tmpfs";
    }
    return "其他";
}

} // namespace

int main() {
    std::printf("E6 搬移检测 —— A/B 同设备(ext4),C 跨设备(tmpfs, /dev/shm)\n");
    print_inotify_limits();

    std::string rm = "rm -rf '" + dir_a + "' '" + dir_b + "' '" + dir_c + "'";
    if (::system(rm.c_str()) != 0) {
        return 1;
    }
    if (::mkdir("/home/charliechen/l06_scratch/e6", 0755) == -1 && errno != EEXIST) {
        std::perror("mkdir");
        return 1;
    }
    if (::mkdir(dir_a.c_str(), 0755) == -1 || ::mkdir(dir_b.c_str(), 0755) == -1 ||
        ::mkdir(dir_c.c_str(), 0755) == -1) {
        std::perror("mkdir");
        return 1;
    }

    // 设备口径:打印三个目录各自的文件系统
    for (const auto* d : {dir_a.c_str(), dir_b.c_str(), dir_c.c_str()}) {
        struct statfs st{};
        if (::statfs(d, &st) == 0) {
            std::printf("statfs(%-32s) = %s (f_type=0x%lx)\n", d, fs_name(st.f_type), st.f_type);
        }
    }

    unique_fd ifd{sys_call("inotify_init1", ::inotify_init1, IN_NONBLOCK | O_CLOEXEC)};
    event_reader reader;

    const int wd_a =
        static_cast<int>(sys_call("add_watch(A)", ::inotify_add_watch, ifd.get(), dir_a.c_str(),
                                  static_cast<std::uint32_t>(IN_ALL_EVENTS)));
    const int wd_b =
        static_cast<int>(sys_call("add_watch(B)", ::inotify_add_watch, ifd.get(), dir_b.c_str(),
                                  static_cast<std::uint32_t>(IN_ALL_EVENTS)));
    const int wd_c =
        static_cast<int>(sys_call("add_watch(C)", ::inotify_add_watch, ifd.get(), dir_c.c_str(),
                                  static_cast<std::uint32_t>(IN_ALL_EVENTS)));
    std::printf("wd_a=%d → A/,wd_b=%d → B/,wd_c=%d → C/\n", wd_a, wd_b, wd_c);

    // 源文件各备一个
    for (const char* name : {"f_samefs", "f_xdev"}) {
        std::string p = dir_a + "/" + name;
        int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
        if (fd == -1) {
            std::perror("open");
            return 1;
        }
        ::write(fd, "payload", 7);
        ::close(fd);
    }
    drain_print(ifd.get(), reader, "(备场的 create 事件,不关心)");

    // ---- 组一:同设备跨目录 mv ----
    std::printf("\n==== mv A/f_samefs B/  (ext4 → ext4,rename(2) 一步到位)====\n");
    if (::rename((dir_a + "/f_samefs").c_str(), (dir_b + "/f_samefs").c_str()) == -1) {
        std::perror("rename");
        return 1;
    }
    drain_print(ifd.get(), reader, "");

    // ---- 组二:跨设备 mv ----
    std::printf("\n==== 先看 rename(2) 跨设备会怎样 ====\n");
    int rc = ::rename((dir_a + "/f_xdev").c_str(), (dir_c + "/f_xdev").c_str());
    std::printf("rename(A/f_xdev, C/f_xdev) = %d, errno = %d (%s)\n", rc, errno,
                std::strerror(errno));
    drain_print(ifd.get(), reader, "");

    std::printf("\n==== mv 走的退路:复制 + 删除(自己照 mv 的做法演一遍)====\n");
    {
        // 复制:open 源 → 读 → 写目的 → close → 保全权限(chmod)→ unlink 源
        int in = ::open((dir_a + "/f_xdev").c_str(), O_RDONLY | O_CLOEXEC);
        int out =
            ::open((dir_c + "/f_xdev").c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (in == -1 || out == -1) {
            std::perror("open copy");
            return 1;
        }
        char buf[4096];
        ssize_t r;
        while ((r = ::read(in, buf, sizeof buf)) > 0) {
            ::write(out, buf, static_cast<std::size_t>(r));
        }
        ::close(out);
        ::close(in);
        ::chmod((dir_c + "/f_xdev").c_str(), 0644);
        ::unlink((dir_a + "/f_xdev").c_str());
    }
    drain_print(ifd.get(), reader, "");

    std::printf("\n==== 结论对照 ====\n");
    std::printf("同设备 rename:A 侧 IN_MOVED_FROM 与 B 侧 IN_MOVED_TO 的"
                "cookie 相同、wd 不同 —— 配对成立\n");
    std::printf("跨设备 mv:源侧只有 OPEN/ACCESS/CLOSE_NOWRITE/DELETE,"
                "目的侧只有 CREATE/MODIFY/CLOSE_WRITE/ATTRIB,"
                "cookie 全程为 0 —— 事件流里不存在「这次搬移」,"
                "监控端只能自己猜(时间窗 + 同名)或放弃\n");
    return 0;
}
