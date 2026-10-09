// E6: 超时也是一个请求 (IORING_OP_TIMEOUT / IORING_OP_LINK_TIMEOUT)
//   甲: 两个独立 TIMEOUT (350ms 与 100ms, 故意反着提交) -> 完成顺序跟墙钟走
//   乙: 给一个永远等不来数据的 read 配 LINK_TIMEOUT 300ms -> 到点 read 被 -ECANCELED
//   丙: 同样的组合, 100ms 时数据来了 -> read 正常完成, 超时请求被 -ECANCELED
// 对照篇2 的 timerfd: 那边是"fd 进表", 这边是"请求进环", 收获都在同一个 CQ。
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <liburing.h>
#include <unistd.h>

static long now_ms() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int main() {
    io_uring ring;
    io_uring_queue_init(8, &ring, 0);
    long t0 = now_ms();

    // ---- 甲: 两个独立超时, 提交顺序与到期顺序相反 ----
    {
        io_uring_sqe* s1 = io_uring_get_sqe(&ring);
        __kernel_timespec ts1{0, 350000000};
        io_uring_prep_timeout(s1, &ts1, 0, 0);
        io_uring_sqe_set_data64(s1, 350);
        io_uring_sqe* s2 = io_uring_get_sqe(&ring);
        __kernel_timespec ts2{0, 100000000};
        io_uring_prep_timeout(s2, &ts2, 0, 0);
        io_uring_sqe_set_data64(s2, 100);
        io_uring_submit(&ring);
        std::printf("甲: 反序提交两个 TIMEOUT (350ms 先提交, 100ms 后提交):\n");
        for (int i = 0; i < 2; ++i) {
            io_uring_cqe* cqe = nullptr;
            io_uring_wait_cqe(&ring, &cqe);
            std::printf("  t+%4ld ms 收到 %lldms 的完成 (res=%d, %s)\n", now_ms() - t0,
                        (unsigned long long)io_uring_cqe_get_data64(cqe), cqe->res,
                        cqe->res == -ETIME ? "到点" : "?");
            io_uring_cqe_seen(&ring, cqe);
        }
    }

    // ---- 乙: 链上超时把等不来的 read 取消 ----
    int pp[2];
    (void)!pipe(pp);
    fcntl(pp[0], F_SETFL, O_NONBLOCK);
    {
        io_uring_sqe* s1 = io_uring_get_sqe(&ring);
        unsigned char buf[16];
        io_uring_prep_read(s1, pp[0], buf, sizeof buf, 0);
        io_uring_sqe_set_data64(s1, 1);
        s1->flags |= IOSQE_IO_LINK;
        io_uring_sqe* s2 = io_uring_get_sqe(&ring);
        __kernel_timespec ts{0, 300000000};
        io_uring_prep_link_timeout(s2, &ts, 0);
        io_uring_sqe_set_data64(s2, 2);
        io_uring_submit(&ring);
        std::printf("\n乙: read(空管道) 挂 LINK_TIMEOUT 300ms, 没人写数据:\n");
        for (int i = 0; i < 2; ++i) {
            io_uring_cqe* cqe = nullptr;
            io_uring_wait_cqe(&ring, &cqe);
            std::printf("  t+%4ld ms 操作%llu res=%d (%s)\n", now_ms() - t0,
                        (unsigned long long)io_uring_cqe_get_data64(cqe), cqe->res,
                        cqe->res == -ECANCELED ? "被超时取消" : "到点");
            io_uring_cqe_seen(&ring, cqe);
        }
    }

    // ---- 丙: 100ms 时数据来了, read 正常完成 ----
    {
        io_uring_sqe* s1 = io_uring_get_sqe(&ring);
        static unsigned char buf[16];
        io_uring_prep_read(s1, pp[0], buf, sizeof buf, 0);
        io_uring_sqe_set_data64(s1, 1);
        s1->flags |= IOSQE_IO_LINK;
        io_uring_sqe* s2 = io_uring_get_sqe(&ring);
        __kernel_timespec ts{0, 500000000};
        io_uring_prep_link_timeout(s2, &ts, 0);
        io_uring_sqe_set_data64(s2, 2);
        io_uring_submit(&ring);
        // 100ms 后写入, read 应该先完成
        timespec nap{0, 100000000};
        nanosleep(&nap, nullptr);
        (void)!write(pp[1], "hello", 5);
        std::printf("\n丙: 同样的组合, 100ms 时写入 5 字节 (预算 500ms):\n");
        for (int i = 0; i < 2; ++i) {
            io_uring_cqe* cqe = nullptr;
            io_uring_wait_cqe(&ring, &cqe);
            std::printf("  t+%4ld ms 操作%llu res=%d (%s)\n", now_ms() - t0,
                        (unsigned long long)io_uring_cqe_get_data64(cqe), cqe->res,
                        cqe->res >= 0 ? "读到了" : "被撤掉");
            io_uring_cqe_seen(&ring, cqe);
        }
    }
    io_uring_queue_exit(&ring);
    return 0;
}
