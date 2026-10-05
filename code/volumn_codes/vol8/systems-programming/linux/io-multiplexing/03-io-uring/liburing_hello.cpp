#include <cstdio>
#include <liburing.h>
int main() {
    struct io_uring ring;
    int r = io_uring_queue_init(8, &ring, 0);
    if (r < 0) {
        printf("queue_init failed: %d\n", r);
        return 1;
    }
    printf("liburing version: %d.%d\n", io_uring_major_version(), io_uring_minor_version());
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data64(sqe, 42);
    int submitted = io_uring_submit(&ring);
    struct io_uring_cqe* cqe = nullptr;
    int ret = io_uring_wait_cqe(&ring, &cqe);
    printf("submitted=%d wait=%d cqe res=%d user_data=%llu\n", submitted, ret,
           cqe ? cqe->res : -999, cqe ? (unsigned long long)io_uring_cqe_get_data64(cqe) : 0);
    io_uring_queue_exit(&ring);
    return 0;
}
