// E5: 批量经济性: 4096 个 4KiB 读, read(2) 逐个调 vs io_uring 256 一批
// 16 MiB 文件先整读一遍预热页缓存, 两边各 5 轮取中位, 计时 CLOCK_MONOTONIC。
// 另带一个 SQPOLL 探针: IORING_SETUP_SQPOLL 在本机能不能建 (内核线程代收提交)。
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <liburing.h>
#include <unistd.h>
#include <vector>

static const char* kPath = "/home/charliechen/ch04_scratch/u_big.bin";
static const int kBlocks = 4096; // 16 MiB / 4 KiB
static const int kBatch = 256;
static const int kBlockSz = 4096;

static long now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

static void seed_and_warm() {
    int fd = open(kPath, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        std::perror("open");
        return;
    }
    std::vector<unsigned char> buf(kBlockSz, 0xAB);
    for (int i = 0; i < kBlocks; ++i)
        if (write(fd, buf.data(), buf.size()) != (ssize_t)buf.size()) {
            std::perror("seed");
            return;
        }
    // 预热页缓存
    unsigned char t[kBlockSz];
    lseek(fd, 0, SEEK_SET);
    while (read(fd, t, sizeof t) > 0) {
    }
    close(fd);
}

static double bench_read(int reps) {
    int fd = open(kPath, O_RDONLY);
    std::vector<double> ms;
    unsigned char buf[kBlockSz];
    for (int rep = 0; rep < reps; ++rep) {
        long t0 = now_ns();
        for (int i = 0; i < kBlocks; ++i)
            if (pread(fd, buf, sizeof buf, (long long)i * kBlockSz) < 0) {
            }
        ms.push_back((now_ns() - t0) / 1e6);
    }
    close(fd);
    std::sort(ms.begin(), ms.end());
    return ms[ms.size() / 2];
}

static double bench_uring(int reps, int* submits, int* waits) {
    int fd = open(kPath, O_RDONLY);
    std::vector<double> ms;
    unsigned char* buf = new unsigned char[kBatch * kBlockSz];
    int sub_total = 0, wait_total = 0;
    for (int rep = 0; rep < reps; ++rep) {
        io_uring ring;
        if (io_uring_queue_init(kBatch, &ring, 0) < 0)
            break;
        long t0 = now_ns();
        int done = 0, sub_calls = 0, wait_calls = 0, sqe_sum = 0, cqe_sum = 0;
        while (done < kBlocks) {
            int n = std::min(kBatch, kBlocks - done);
            for (int i = 0; i < n; ++i) {
                io_uring_sqe* sqe = io_uring_get_sqe(&ring);
                io_uring_prep_read(sqe, fd, buf + (size_t)i * kBlockSz, kBlockSz,
                                   (long long)(done + i) * kBlockSz);
                io_uring_sqe_set_data64(sqe, done + i);
            }
            ++sub_calls;
            sqe_sum += io_uring_submit(&ring); // 一次调用, n 个 SQE 进环
            io_uring_cqe* cqe = nullptr;
            ++wait_calls;
            if (io_uring_wait_cqes(&ring, &cqe, n, nullptr, nullptr) == 0 && cqe) {
                io_uring_cqe_seen(&ring, cqe);
                ++cqe_sum;
            }
            while (cqe_sum < sqe_sum) { // 其余从共享环里拿, 不进内核
                if (io_uring_peek_cqe(&ring, &cqe) != 0 || !cqe)
                    continue;
                io_uring_cqe_seen(&ring, cqe);
                ++cqe_sum;
            }
            done += n;
        }
        ms.push_back((now_ns() - t0) / 1e6);
        sub_total += sub_calls;
        wait_total += wait_calls;
        (void)cqe_sum;
        io_uring_queue_exit(&ring);
    }
    delete[] buf;
    close(fd);
    std::sort(ms.begin(), ms.end());
    *submits = sub_total / reps;
    *waits = wait_total / reps;
    return ms[ms.size() / 2];
}

int main() {
    seed_and_warm();
    std::printf("16 MiB (4096 x 4KiB), 页缓存已预热, 5 轮取中位:\n");
    double a = bench_read(5);
    int sub = 0, wt = 0;
    double b = bench_uring(5, &sub, &wt);
    std::printf("read(2) 逐块 : %8.1f ms (%d 次系统调用)\n", a, kBlocks);
    std::printf(
        "io_uring %d 一批: %8.1f ms (submit 调用 %d 次 + wait 调用 %d 次, 其余收割是用户态 peek)\n",
        kBatch, b, sub, wt);
    std::printf("每块均摊: read %.0f ns, io_uring %.0f ns\n", a * 1e6 / kBlocks, b * 1e6 / kBlocks);

    // ---- SQPOLL 探针 ----
    io_uring ring;
    int r = io_uring_queue_init(8, &ring, IORING_SETUP_SQPOLL);
    if (r < 0) {
        std::printf("\nSQPOLL 探针: io_uring_queue_init(IORING_SETUP_SQPOLL) = %d (%s)\n", r,
                    std::strerror(-r));
        return 0;
    }
    io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data64(sqe, 7);
    int submitted = io_uring_submit(&ring); // SQPOLL: 只写环, 不进内核催
    std::printf("\nSQPOLL 探针: 建环成功, submit=%d (只写共享环, 不催内核)\n", submitted);
    io_uring_cqe* cqe = nullptr;
    for (int i = 0; i < 1000 && !cqe; ++i) { // 干等内核线程代收
        io_uring_peek_cqe(&ring, &cqe);
        if (!cqe)
            usleep(1000);
    }
    std::printf("SQPOLL: 内核线程 %s把 NOP 干完 (res=%d)\n", cqe ? "已" : "没有及时",
                cqe ? cqe->res : -1);
    io_uring_queue_exit(&ring);
    return 0;
}
