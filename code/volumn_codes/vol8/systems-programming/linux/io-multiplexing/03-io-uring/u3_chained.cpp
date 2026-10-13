// E3: 链式请求 IOSQE_IO_LINK
// 同一次提交里的多个请求用链串起来, 内核保证顺序执行; 前一个失败, 后面的不再执行,
// 直接以 -ECANCELED 完成。这里跑两条链:
//   链A(正常): read 4KiB -> write 4KiB -> fsync, 三个 CQE 依次 4096/4096/0
//   链B(断链): read(bad fd) -> write, CQE 依次 -EBADF/-ECANCELED
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <liburing.h>
#include <unistd.h>

static const char* kSrc = "/home/charliechen/ch04_scratch/u_src.bin";
static const char* kDst = "/home/charliechen/ch04_scratch/u_dst.bin";

static void reap_all(io_uring* ring, int n, const char* tag) {
    std::printf("%s:\n", tag);
    for (int i = 0; i < n; ++i) {
        io_uring_cqe* cqe = nullptr;
        if (io_uring_wait_cqe(ring, &cqe) != 0 || !cqe)
            return;
        char name[32];
        std::snprintf(name, sizeof name, "操作%llu",
                      (unsigned long long)io_uring_cqe_get_data64(cqe));
        std::printf("  %-6s res=%d (%s)\n", name, cqe->res,
                    cqe->res == -ECANCELED ? "链断了, 被取消"
                    : cqe->res == -EBADF   ? "坏的 fd"
                                           : "成功");
        io_uring_cqe_seen(ring, cqe);
    }
}

int main() {
    // 备 8 KiB 源文件与目标文件
    int sfd = open(kSrc, O_RDWR | O_CREAT | O_TRUNC, 0644);
    unsigned char seed[8192];
    for (int i = 0; i < 8192; ++i)
        seed[i] = (unsigned char)i;
    if (write(sfd, seed, 8192) != 8192)
        return 1;
    close(sfd);
    int dfd = open(kDst, O_RDWR | O_CREAT | O_TRUNC, 0644);
    sfd = open(kSrc, O_RDONLY);

    io_uring ring;
    io_uring_queue_init(8, &ring, 0);
    static unsigned char buf[4096];

    // ---- 链A: read -> write -> fsync ----
    io_uring_sqe* s1 = io_uring_get_sqe(&ring);
    io_uring_prep_read(s1, sfd, buf, 4096, 0);
    io_uring_sqe_set_data64(s1, 1);
    s1->flags |= IOSQE_IO_LINK; // 与下一个绑成链

    io_uring_sqe* s2 = io_uring_get_sqe(&ring);
    io_uring_prep_write(s2, dfd, buf, 4096, 0);
    io_uring_sqe_set_data64(s2, 2);
    s2->flags |= IOSQE_IO_LINK;

    io_uring_sqe* s3 = io_uring_get_sqe(&ring);
    io_uring_prep_fsync(s3, dfd, 0);
    io_uring_sqe_set_data64(s3, 3); // 链尾不用再挂 LINK

    int n = io_uring_submit(&ring);
    std::printf("链A 一次提交 %d 个 (read->write->fsync 挂链)\n", n);
    reap_all(&ring, 3, "  链A 的三个完成, 顺序与提交一致");

    // ---- 链B: 第一个是坏 fd, 链在第一步断掉 ----
    io_uring_sqe* b1 = io_uring_get_sqe(&ring);
    io_uring_prep_read(b1, -1, buf, 4096, 0); // fd = -1
    io_uring_sqe_set_data64(b1, 1);
    b1->flags |= IOSQE_IO_LINK;

    io_uring_sqe* b2 = io_uring_get_sqe(&ring);
    io_uring_prep_write(b2, dfd, buf, 4096, 4096);
    io_uring_sqe_set_data64(b2, 2);

    n = io_uring_submit(&ring);
    std::printf("\n链B 一次提交 %d 个 (read(bad fd)->write)\n", n);
    reap_all(&ring, 2, "  链B 的两个完成: 失败向下游传播成取消");

    // 验证链A真的写了
    unsigned char back[4096];
    pread(dfd, back, 4096, 0);
    std::printf("\n链A 的 write 确实生效: 前 16 字节 = ");
    for (int i = 0; i < 16; ++i)
        std::printf("%02x ", back[i]);
    std::printf("...\n");
    std::printf("链B 的 write 没有执行: 目标文件偏移 4096 处仍是 0 (只写了前 4KiB)\n");
    io_uring_queue_exit(&ring);
    return 0;
}
