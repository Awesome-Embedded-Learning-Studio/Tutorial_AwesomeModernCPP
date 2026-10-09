// E6: process_vm_readv —— 跨进程读内存的专用通道(一句话级演示)
//   fork 后子进程往自己的全局数组写 0xC0DE 模式(父进程的副本保持旧值),
//   父进程用 process_vm_readv 直接把子进程地址空间里那段抄回来,管道同步。
//   附两条错误路径:读子进程未映射地址 -> EFAULT;读不存在的 pid -> ESRCH。
//   这是 ptrace 之外的"只借读一眼"通道,进程间共享数据的主力仍是共享内存。
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

int g_payload[8]; // fork 后子进程写它,父进程的副本不动

int main() {
    for (int i = 0; i < 8; ++i) {
        g_payload[i] = i;
    } // 父子分家前的初值

    int pipefd[2];
    ::pipe(pipefd);
    const pid_t pid = ::fork();
    if (pid == 0) { // 子进程:改自己的副本,然后通知
        for (int i = 0; i < 8; ++i) {
            g_payload[i] = 0xC0DE0000 + i;
        }
        char c = 'r';
        ::write(pipefd[1], &c, 1);
        ::pause(); // 等父亲读完再死,避免竞态
        ::_exit(0);
    }
    char c = 0;
    ::read(pipefd[0], &c, 1); // 等子进程写完

    int local[8] = {};
    struct iovec local_iov{local, sizeof local};
    struct iovec remote_iov{g_payload, sizeof g_payload}; // fork 继承,地址同值
    const ssize_t n = ::process_vm_readv(pid, &local_iov, 1, &remote_iov, 1, 0);
    std::printf(
        "parent read child's memory: %zd bytes ->\n   child  has [0x%08x 0x%08x .. 0x%08x]\n"
        "   parent has [0x%08x 0x%08x .. 0x%08x] (untouched, COW)\n",
        n, local[0], local[1], local[7], g_payload[0], g_payload[1], g_payload[7]);

    struct iovec bad_iov{reinterpret_cast<void*>(0x1), 64}; // 子进程里没映射
    const ssize_t bad = ::process_vm_readv(pid, &local_iov, 1, &bad_iov, 1, 0);
    std::printf("read unmapped remote addr : rc=%zd errno=%d (%s)\n", bad, bad == -1 ? errno : 0,
                std::strerror(errno));
    const ssize_t nopid = ::process_vm_readv(1 << 20, &local_iov, 1, &remote_iov, 1, 0);
    std::printf("read bogus pid            : rc=%zd errno=%d (%s)\n", nopid,
                nopid == -1 ? errno : 0, std::strerror(errno));

    ::kill(pid, SIGTERM);
    ::waitpid(pid, nullptr, 0);
    return 0;
}
