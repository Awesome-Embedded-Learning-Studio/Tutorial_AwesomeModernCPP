// E4: MAP_SHARED 跨进程可见性(现文只有断言、没有演示的最大缺口)
// 父子「各自」open + 各自 mmap 同一文件同一区间(MAP_SHARED),地址不同;
// 子进程改 1 字节后经管道通知(管道里顺带捎回子进程的映射地址,子进程全程不 printf);
// 父进程从自己的映射读、再用 pread 交叉验证 —— 全程没有 msync。
// 文件放在 ext4 上(/tmp 是 tmpfs,可见性语义相同,但真盘更有说服力)。
#include "article.hpp"

#include <cstdint>
#include <cstring>
#include <print>
#include <string_view>
#include <sys/wait.h>

int main()
{
    const char* path = "/home/charliechen/l02_scratch/e4.bin";
    {
        unique_fd w{sys_call("open", ::open, path, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
        sys_call("write", ::write, w.get(), "AAAABBBBCCCCDDDD", 16);
    }

    unique_fd fd{sys_call("open", ::open, path, O_RDWR)};
    mapped_region mine(fd, 16, PROT_READ | PROT_WRITE, MAP_SHARED);
    auto cv = [](const unsigned char* p) { return reinterpret_cast<const char*>(p); };
    std::print("parent : mmap MAP_SHARED 16 B at 0x{:x}, view = {:.16s}\n",
               reinterpret_cast<std::uintptr_t>(mine.data()), cv(mine.data()));

    int pfd[2];
    sys_call("pipe", ::pipe, pfd);

    pid_t pid = ::fork();
    if (pid == 0) {                                   // ---- 子进程 ----
        unique_fd own{sys_call("open", ::open, path, O_RDWR)}; // 自己的 open file description
        void* p = ::mmap(nullptr, 16, PROT_READ | PROT_WRITE, MAP_SHARED, own.get(), 0);
        if (p == MAP_FAILED) { ::_exit(9); }
        static_cast<unsigned char*>(p)[4] = 'X';      // 改一个字节,落在页缓存
        if (static_cast<volatile unsigned char*>(p)[4] != 'X') { ::_exit(8); }
        struct { char tag; std::uint64_t addr; } msg{'R', reinterpret_cast<std::uint64_t>(p)};
        // 结构体有对齐填充,按 offsetof 取地址字段,别猜布局
        const unsigned off = static_cast<unsigned>(__builtin_offsetof(decltype(msg), addr));
        if (::write(pfd[1], &msg, off + sizeof msg.addr) != static_cast<ssize_t>(off + sizeof msg.addr)) { ::_exit(7); }
        ::_exit(0);                                   // 正常退出码 0,父进程按 RAII 口径收尸
    }

    char buf[32] {};
    constexpr unsigned kAddrOff = 8; // offsetof(char tag; uint64 addr) 里的填充是 7,addr 在第 8 字节
    const unsigned want = kAddrOff + sizeof(std::uint64_t);
    for (unsigned got = 0; got < want;) {             // 管道读也要循环(01 篇的教诲)
        ssize_t n = sys_call("read-pipe", ::read, pfd[0], buf + got, want - got);
        if (n == 0) { break; }
        got += static_cast<unsigned>(n);
    }
    std::uint64_t child_addr = 0;
    std::memcpy(&child_addr, buf + kAddrOff, sizeof child_addr);
    std::print("child  : its OWN mmap at 0x{:x}, wrote 'X' at [4], pinged pipe\n", child_addr);

    std::print("parent : after pipe ping (no msync anywhere):\n");
    std::print("  own mapping [4]     = {}   <- 子进程写的字节,父亲自己的映射直接可见\n",
               static_cast<char>(mine.data()[4]));
    char pv[16] {};
    sys_call("pread", ::pread, fd.get(), pv, 16, 0);
    std::print("  pread(fd, 16 B)     = {:.16s}   <- 独立 syscall 路径问文件,同一个答案\n", pv);
    std::print("  whole view          = {:.16s}\n", cv(mine.data()));

    int st = 0;
    sys_call("waitpid", ::waitpid, pid, &st, 0);
    std::print("waitpid: WIFEXITED={} code={} (0 = child clean)\n",
               WIFEXITED(st), WEXITSTATUS(st));
    return 0;
}
