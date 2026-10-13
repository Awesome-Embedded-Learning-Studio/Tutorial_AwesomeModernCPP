// E1a 管道容量探针:F_GETPIPE_SZ 读默认容量,F_SETPIPE_SZ 调节,
// /proc/sys/fs/pipe-max-size 是非特权进程能调到的上限。
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e1_capacity.cpp -o e1_capacity
#include "ipc_util.hpp"

#include <cstdio>
#include <cstring>

int main() {
    int pfd[2];
    sys_call("pipe", pipe, pfd);
    const int def =
        sys_call("F_GETPIPE_SZ", [](int fd) { return fcntl(fd, F_GETPIPE_SZ); }, pfd[1]);
    std::printf("新建管道:fd=%d(读)、%d(写),F_GETPIPE_SZ = %d 字节(%d 页 × 4096)\n", pfd[0], pfd[1],
                def, def / 4096);

    // 调小到一页:观察「实际容量」以 fcntl 返回值为准
    errno = 0;
    int got = fcntl(pfd[1], F_SETPIPE_SZ, 4096);
    std::printf("F_SETPIPE_SZ(4096) 返回 %d → 再探 F_GETPIPE_SZ = %d 字节\n", got,
                sys_call("F_GETPIPE_SZ", [](int fd) { return fcntl(fd, F_GETPIPE_SZ); }, pfd[1]));

    // 非整页请求:向上取整到页的倍数
    errno = 0;
    got = fcntl(pfd[1], F_SETPIPE_SZ, 5000);
    const int rounded =
        sys_call("F_GETPIPE_SZ", [](int fd) { return fcntl(fd, F_GETPIPE_SZ); }, pfd[1]);
    std::printf("F_SETPIPE_SZ(5000) 返回 %d → 实际容量 %d 字节(向上取整到 %d 页)\n", got, rounded,
                rounded / 4096);

    // 调大到 256 KiB
    errno = 0;
    got = fcntl(pfd[1], F_SETPIPE_SZ, 256 * 1024);
    const int big =
        sys_call("F_GETPIPE_SZ", [](int fd) { return fcntl(fd, F_GETPIPE_SZ); }, pfd[1]);
    std::printf("F_SETPIPE_SZ(262144) 返回 %d → 实际容量 %d 字节(%d 页)\n", got, big, big / 4096);

    // 上限:/proc/sys/fs/pipe-max-size(非特权 F_SETPIPE_SZ 请求超过它 → EPERM)
    std::FILE* f = std::fopen("/proc/sys/fs/pipe-max-size", "r");
    long pmax = -1;
    if (f) {
        std::fscanf(f, "%ld", &pmax);
        std::fclose(f);
    }
    std::printf("/proc/sys/fs/pipe-max-size = %ld(非特权 F_SETPIPE_SZ 的上限)\n", pmax);

    errno = 0;
    got = fcntl(pfd[1], F_SETPIPE_SZ, 2 * 1024 * 1024);
    std::printf("F_SETPIPE_SZ(%ld 超上限) 返回 %d,errno=%d (%s)——想要更大得有 CAP_SYS_RESOURCE\n",
                2L * 1024 * 1024, got, errno, std::strerror(errno));

    // 回到默认,给 e1_write_full 呼应:默认容量就是 16 页
    errno = 0;
    fcntl(pfd[1], F_SETPIPE_SZ, def);
    close(pfd[0]);
    close(pfd[1]);
    return 0;
}
