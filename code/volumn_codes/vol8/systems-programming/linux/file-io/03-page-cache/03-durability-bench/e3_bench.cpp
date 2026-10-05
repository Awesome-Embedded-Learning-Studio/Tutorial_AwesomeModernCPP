// E3: 同一份数据走四条持久化路径,各计时:
//   plain    : write() 后不管 —— 只进页缓存
//   fsync    : write 循环 + 结尾一次 fsync(计时含 fsync)
//   fdatasync: write 循环 + 结尾一次 fdatasync(计时含 fdatasync)
//   osync    : open(O_SYNC),每次 write 都同步落盘(计时天然覆盖)
// 计时用 clock_gettime(CLOCK_MONOTONIC)。
//
// 用法: ./e3_bench <mode> <path> <totalMiB> <chunkKiB>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

static long long now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: %s <plain|fsync|fdatasync|osync> <path> <totalMiB> <chunkKiB>\n",
                     argv[0]);
        return 2;
    }
    const char* mode = argv[1];
    const char* path = argv[2];
    const long long total = atoll(argv[3]) << 20; // MiB -> B
    const long long chunk = atoll(argv[4]) << 10; // KiB -> B

    int flags = O_WRONLY | O_CREAT | O_TRUNC;
    bool do_fsync = false, do_fdatasync = false;
    if (!strcmp(mode, "plain")) {
    } else if (!strcmp(mode, "fsync")) {
        do_fsync = true;
    } else if (!strcmp(mode, "fdatasync")) {
        do_fdatasync = true;
    } else if (!strcmp(mode, "osync")) {
        flags |= O_SYNC;
    } else {
        std::fprintf(stderr, "unknown mode %s\n", mode);
        return 2;
    }

    int fd = open(path, flags, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    std::vector<char> buf(static_cast<size_t>(chunk)); // 内容对计时无影响,零缓冲即可
    long long t0 = now_ns();
    for (long long off = 0; off < total; off += chunk) {
        long long done = 0;
        while (done < chunk) {
            ssize_t n = write(fd, buf.data() + done, static_cast<size_t>(chunk - done));
            if (n < 0) {
                perror("write");
                return 1;
            }
            done += n;
        }
    }
    if (do_fsync && fsync(fd) != 0) {
        perror("fsync");
        return 1;
    }
    if (do_fdatasync && fdatasync(fd) != 0) {
        perror("fdatasync");
        return 1;
    }
    long long t1 = now_ns();
    close(fd);

    double ms = static_cast<double>(t1 - t0) / 1e6;
    // 机器可读的一行,外层 e3_run.sh 抓 ELAPSED_MS 汇总
    std::printf("RESULT mode=%s totalMiB=%lld chunkKiB=%lld elapsed_ms=%.1f mib_per_s=%.0f\n", mode,
                total >> 20, chunk >> 10, ms, (total >> 20) / (ms / 1000.0));
    return 0;
}
