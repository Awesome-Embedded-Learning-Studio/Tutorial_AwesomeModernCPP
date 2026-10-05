// E1: eventfd 的计数器语义
// 一个 8 字节的无符号计数器: write 加, read 取走。两种取法:
//   默认   : read 返回累计值并把计数器清零
//   信号量 : read 每次只减 1, 返回 1
// 越界与尺寸错配都有明确的 errno。
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/eventfd.h>
#include <unistd.h>

static void try_read(int fd, const char* tag) {
    uint64_t v = 0;
    ssize_t r = read(fd, &v, 8);
    if (r < 0)
        std::printf("  read(%s) = -1 errno=%d (%s)\n", tag, errno, std::strerror(errno));
    else
        std::printf("  read(%s) = %llu\n", tag, (unsigned long long)v);
}

static void try_write(int fd, uint64_t v, const char* tag) {
    ssize_t r = write(fd, &v, 8);
    if (r < 0)
        std::printf("  write(%s, %llu) = -1 errno=%d (%s)\n", tag, (unsigned long long)v, errno,
                    std::strerror(errno));
    else
        std::printf("  write(%s, %llu) = %zd\n", tag, (unsigned long long)v, r);
}

int main() {
    // ---- 默认模式: 读走即清零 ----
    int efd = eventfd(0, EFD_NONBLOCK);
    std::printf("[默认模式 efd=%d]\n", efd);
    try_write(efd, 1, "第1次");
    try_write(efd, 2, "第2次");
    try_write(efd, 5, "第3次");
    std::printf("三次 write 之后计数器 = 1+2+5, 一次 read 全取走:\n");
    try_read(efd, "取值");
    try_read(efd, "再读");

    // ---- 信号量模式: 每次减 1 ----
    int efs = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
    std::printf("\n[信号量模式 EFD_SEMAPHORE efs=%d]\n", efs);
    try_write(efs, 5, "一次写 5");
    for (int i = 1; i <= 6; ++i)
        try_read(efs, "第n次");

    // ---- 溢出: 计数器不够加 ----
    int efo = eventfd(0, EFD_NONBLOCK);
    std::printf("\n[溢出演练 efo=%d]\n", efo);
    try_write(efo, 0xFFFFFFFFFFFFFFFFULL - 1, "写到只差 1 满");
    try_write(efo, 5, "再加 5");

    // ---- 尺寸错配 ----
    int efe = eventfd(0, EFD_NONBLOCK);
    std::printf("\n[尺寸错配 efe=%d]\n", efe);
    uint32_t small = 1;
    ssize_t r = write(efe, &small, 4);
    std::printf("  write(4 字节) = %zd errno=%d (%s)\n", r, errno, std::strerror(errno));
    uint32_t out4 = 0;
    r = read(efe, &out4, 4);
    std::printf("  read(4 字节)  = %zd errno=%d (%s)\n", r, errno, std::strerror(errno));
    return 0;
}
