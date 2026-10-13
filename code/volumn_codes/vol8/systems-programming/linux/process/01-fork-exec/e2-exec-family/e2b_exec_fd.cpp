// E2b exec 换映像时的 fd 生死:不带 CLOEXEC 的穿过,带 CLOEXEC 的被关
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -o e2b_exec_fd e2b_exec_fd.cpp
// 与 L01 的接续:L01 在 dup/fcntl 层面讲过 FD_CLOEXEC 的含义,这里给 exec 前后的
//              /proc/self/fd 实拍对照。同时证 pid 不变、共享偏移延续。
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

static const char* kPath = "/home/charliechen/lp01_scratch/e2b_data.bin";

static std::string self_path() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) {
        std::perror("readlink");
        std::exit(1);
    }
    buf[n] = '\0';
    return buf;
}

static std::string list_fds() {
    std::string out;
    DIR* d = opendir("/proc/self/fd");
    if (!d)
        return "    (打不开 /proc/self/fd)\n";
    std::vector<int> fds;
    while (dirent* e = readdir(d))
        if (e->d_name[0] >= '0' && e->d_name[0] <= '9')
            fds.push_back(std::atoi(e->d_name));
    closedir(d);
    std::sort(fds.begin(), fds.end());
    for (int fd : fds) {
        char link[64], target[512];
        std::snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
        ssize_t n = readlink(link, target, sizeof target - 1);
        if (n >= 0) {
            target[n] = '\0';
            out += "    fd " + std::to_string(fd) + " -> " + target + "\n";
        }
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "after") == 0) {
        std::printf("[exec 后] pid=%d —— 与 exec 前打印的是同一个数\n", getpid());
        std::printf("[exec 后] /proc/self/fd 清单:\n%s", list_fds().c_str());
        const char* ev = std::getenv("E2B_FD");
        int fd = ev ? std::atoi(ev) : -1;
        if (fd >= 0) {
            char buf[9] = {};
            ssize_t r = read(fd, buf, 8);
            std::printf("[exec 后] fd %d 还能用:再读 8 字节=%s,偏移=%lld(接着 exec 前的位置读)\n",
                        fd, r == 8 ? buf : "(读失败)", (long long)lseek(fd, 0, SEEK_CUR));
        }
        std::printf(
            "[exec 后] 带 O_CLOEXEC 的那个 fd 不在清单里了:同一个文件,一个穿过 exec 一个被关\n");
        return 0;
    }

    // 造 64 字节内容各异的数据
    unsigned char data[64];
    for (int i = 0; i < 64; ++i)
        data[i] = (unsigned char)(48 + i);
    int wfd = open(kPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (wfd < 0 || write(wfd, data, 64) != 64) {
        std::perror("write");
        return 1;
    }
    close(wfd);

    int fd_plain = open(kPath, O_RDONLY);               // 无 CLOEXEC
    int fd_cloexec = open(kPath, O_RDONLY | O_CLOEXEC); // 有 CLOEXEC
    if (fd_plain < 0 || fd_cloexec < 0) {
        std::perror("open");
        return 1;
    }

    std::printf("[exec 前] pid=%d,fd %d(普通)与 fd %d(O_CLOEXEC)指向同一个文件\n", getpid(),
                fd_plain, fd_cloexec);
    std::printf("[exec 前] /proc/self/fd 清单:\n%s", list_fds().c_str());
    char buf[9] = {};
    read(fd_plain, buf, 8);
    std::printf("[exec 前] 先从 fd %d 读 8 字节=%s,偏移=%lld\n", fd_plain, buf,
                (long long)lseek(fd_plain, 0, SEEK_CUR));
    char ev[16];
    std::snprintf(ev, sizeof ev, "%d", fd_plain);
    setenv("E2B_FD", ev, 1);
    std::printf("[exec 前] 现在调 execl 换一个全新的程序映像……\n");
    std::fflush(stdout);

    std::string self = self_path();
    std::string base = self.substr(self.rfind('/') + 1);
    execl(self.c_str(), base.c_str(), "after", (char*)nullptr);
    std::perror("execl"); // exec 成功永远不回来;回来就是失败
    return 1;
}
