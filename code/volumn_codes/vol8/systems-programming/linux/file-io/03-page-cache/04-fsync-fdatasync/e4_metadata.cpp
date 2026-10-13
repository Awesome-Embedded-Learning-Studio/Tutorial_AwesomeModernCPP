// E4: fsync 与 fdatasync 差在哪 —— 构造三种脏法,对比两种 sync 的每次开销。
//
//   fixed : 文件预扩到 1MiB,每轮 pwrite 4KiB 到固定偏移(不改变大小)再 sync
//           -> 脏的只有数据页 + inode 时间戳
//   append: 每轮 pwrite 4KiB 到文件末尾(大小在涨)再 sync
//           -> 数据页 + 必要元数据(大小)都脏,两种 sync 都得刷
//   touch : 不写任何数据,每轮 utimensat 改时间戳再 sync
//           -> 只有元数据脏:fsync 必须把 inode 刷下去,fdatasync 理论上可以跳过
//
// 用法: ./e4_metadata <fixed|append|touch> <fsync|fdatasync> <iters> <path>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

static long long now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <fixed|append|touch> <fsync|fdatasync> <iters> <path>\n",
                     argv[0]);
        return 2;
    }
    const char* scen = argv[1];
    bool datasync = !strcmp(argv[2], "fdatasync");
    long long iters = atoll(argv[3]);
    const char* path = argv[4];

    // 准备阶段:fixed/touch 要一个已经落稳的 1MiB 文件;append 从空文件开始
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    if (strcmp(scen, "append") != 0) {
        std::vector<char> one_mib(1 << 20);
        long long done = 0;
        while (done < (1 << 20)) {
            ssize_t n = write(fd, one_mib.data() + done, (1 << 20) - done);
            if (n < 0) {
                perror("write setup");
                return 1;
            }
            done += n;
        }
        if (fdatasync(fd) != 0) {
            perror("fdatasync setup");
            return 1;
        }
    }

    std::vector<char> blk(4096);
    std::vector<long long> per_op_ns;
    per_op_ns.reserve(static_cast<size_t>(iters));
    long long t_all0 = now_ns();

    for (long long i = 0; i < iters; ++i) {
        if (!strcmp(scen, "fixed")) {
            if (pwrite(fd, blk.data(), blk.size(), 4096) != (ssize_t)blk.size()) {
                perror("pwrite");
                return 1;
            }
        } else if (!strcmp(scen, "append")) {
            off_t end = lseek(fd, 0, SEEK_END);
            if (pwrite(fd, blk.data(), blk.size(), end) != (ssize_t)blk.size()) {
                perror("pwrite");
                return 1;
            }
        } else { // touch
            timespec now[2]{};
            clock_gettime(CLOCK_REALTIME, &now[0]);
            now[1] = now[0];
            // AT_SYMLINK_NOFOLLOW + 路径版:fd 版 futimens 也行,效果一样
            if (utimensat(AT_FDCWD, path, now, 0) != 0) {
                perror("utimensat");
                return 1;
            }
        }
        long long t0 = now_ns();
        if (datasync ? fdatasync(fd) : fsync(fd)) {
            perror("sync");
            return 1;
        }
        per_op_ns.push_back(now_ns() - t0);
    }
    long long t_all1 = now_ns();

    close(fd);
    // 先在 printf 之前算好:median_us 会原地 sort,不能依赖实参求值顺序
    std::sort(per_op_ns.begin(), per_op_ns.end());
    double med_us =
        per_op_ns.empty() ? 0.0 : static_cast<double>(per_op_ns[per_op_ns.size() / 2]) / 1000.0;
    double max_us = per_op_ns.empty() ? 0.0 : static_cast<double>(per_op_ns.back()) / 1000.0;
    std::printf("RESULT scenario=%s sync=%s iters=%lld per_op_median_us=%.1f per_op_max_us=%.1f "
                "total_ms=%.1f\n",
                scen, datasync ? "fdatasync" : "fsync", iters, med_us, max_us,
                static_cast<double>(t_all1 - t_all0) / 1e6);
    return 0;
}
