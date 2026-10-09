// E2: 一次提交 64 个读, 一次等待收 64 个完成 (liburing)
// 数据文件 256 KiB, 每块 4 KiB 填入可校验的模式; user_data 标块号, CQE 带着它回来。
// 对照: 同样的活, read(2) 要 64 次系统调用; 这里 submit 一次 + 收割循环。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <liburing.h>
#include <unistd.h>

static const char* kPath = "/home/charliechen/ch04_scratch/u_data.bin";
static const int kBlocks = 64;
static const int kBlockSz = 4096;

static void seed_file() {
    int fd = open(kPath, O_RDWR | O_CREAT | O_TRUNC, 0644);
    for (int i = 0; i < kBlocks; ++i) {
        unsigned char buf[kBlockSz];
        for (int j = 0; j < kBlockSz; ++j)
            buf[j] = (unsigned char)(i * 7 + j);
        if (write(fd, buf, kBlockSz) != kBlockSz) {
            std::perror("seed");
            std::exit(1);
        }
    }
    close(fd);
}

int main() {
    seed_file();
    int fd = open(kPath, O_RDONLY);
    io_uring ring;
    if (io_uring_queue_init(kBlocks, &ring, 0) < 0) {
        std::perror("queue_init");
        return 1;
    }

    static unsigned char bufs[kBlocks][kBlockSz];
    for (int i = 0; i < kBlocks; ++i) {
        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        io_uring_prep_read(sqe, fd, bufs[i], kBlockSz, (long long)i * kBlockSz);
        io_uring_sqe_set_data64(sqe, i); // 块号当凭证
    }
    int submitted = io_uring_submit(&ring); // 一次 submit, 64 个请求全部进环
    std::printf("一次 io_uring_submit 提交了 %d 个读请求 (各 4 KiB, 各自带偏移)\n", submitted);

    int reaped = 0, wait_calls = 0, peeks = 0, bad = 0;
    long long sum = 0;
    while (reaped < kBlocks) {
        io_uring_cqe* cqe = nullptr;
        int r;
        if (reaped == 0) {
            // 批量等待: 这次调用会真正进内核等 64 个完成
            r = io_uring_wait_cqes(&ring, &cqe, kBlocks, nullptr, nullptr);
            ++wait_calls;
        } else {
            r = io_uring_peek_cqe(&ring, &cqe); // 后续直接从共享环里拿, 不进内核
            ++peeks;
            if (r != 0 || !cqe)
                continue;
        }
        if (r != 0 || !cqe)
            break;
        int block = (int)io_uring_cqe_get_data64(cqe);
        if (cqe->res != kBlockSz)
            ++bad;
        for (int j = 0; j < kBlockSz; ++j)
            if (bufs[block][j] != (unsigned char)(block * 7 + j)) {
                ++bad;
                break;
            }
        sum += cqe->res;
        io_uring_cqe_seen(&ring, cqe);
        ++reaped;
    }
    std::printf("收割 %d 个 CQE (wait 真等 %d 次, 其余 %d 次是用户态 peek), 校验%s, 共 %lld 字节\n",
                reaped, wait_calls, peeks, bad == 0 ? "全部通过 (块号-偏移-内容对上)" : "有错!",
                sum);
    std::printf("系统调用次数对照: read(2) 逐块 = %d 次; io_uring = submit 1 次 + wait 1 次 + peek "
                "不进内核\n",
                kBlocks);
    io_uring_queue_exit(&ring);
    return 0;
}
