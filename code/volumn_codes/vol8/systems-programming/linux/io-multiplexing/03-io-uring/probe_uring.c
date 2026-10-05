#define _GNU_SOURCE
#include <errno.h>
#include <linux/io_uring.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
int main(void) {
    printf("kernel check: io_uring_setup syscall number = %ld\n", (long)__NR_io_uring_setup);
    struct io_uring_params p;
    memset(&p, 0, sizeof(p));
    long fd = syscall(__NR_io_uring_setup, 8, &p);
    printf("io_uring_setup(8, params) = %ld", fd);
    if (fd < 0) {
        printf("  errno=%d (%s)\n", errno, strerror(errno));
        return 1;
    }
    printf("  -> OK, ring fd = %ld\n", fd);
    printf("sq_entries=%u cq_entries=%u flags=0x%x sq_thread_cpu=%u sq_thread_idle=%u "
           "features=0x%x wq_fd=%u\n",
           p.sq_entries, p.cq_entries, p.flags, p.sq_thread_cpu, p.sq_thread_idle, p.features,
           p.wq_fd);
    printf("features bits: NODROP=%d SUBMIT_STABLE=%d RW_CUR_POS=%d CUR_PERSONALITY=%d "
           "SINGLE_MMAP=%d\n",
           !!(p.features & IORING_FEAT_NODROP), !!(p.features & IORING_FEAT_SUBMIT_STABLE),
           !!(p.features & IORING_FEAT_RW_CUR_POS), !!(p.features & IORING_FEAT_CUR_PERSONALITY),
           !!(p.features & IORING_FEAT_SINGLE_MMAP));
    close((int)fd);
    return 0;
}
