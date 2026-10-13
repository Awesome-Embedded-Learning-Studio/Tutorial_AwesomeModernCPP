// E4 的 strace 对照件: 同样读 200 万次时钟,两种走法
//   ./t4_vdso_strace vdso    → 普通 clock_gettime(vDSO 路径)
//   ./t4_vdso_strace syscall → syscall(SYS_clock_gettime,...) 强制真陷入
// 用法: strace -c -e trace=clock_gettime ./t4_vdso_strace vdso   (期待 0 次系统调用)
//       strace -c -e trace=clock_gettime ./t4_vdso_strace syscall (期待 200 万次)
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/syscall.h>
#include <unistd.h>

int main(int argc, char** argv) {
    constexpr int kN = 2000000;
    bool use_syscall = argc > 1 && std::strcmp(argv[1], "syscall") == 0;
    timespec ts{};
    for (int i = 0; i < kN; ++i) {
        if (use_syscall)
            syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts);
        else
            clock_gettime(CLOCK_MONOTONIC, &ts);
    }
    std::printf("done: %d 次读取,走法=%s,最后一次=%lld.%09lld\n", kN,
                use_syscall ? "syscall(2) 真陷入" : "clock_gettime(vDSO)", (long long)ts.tv_sec,
                (long long)ts.tv_nsec);
    return 0;
}
