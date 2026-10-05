// E2: mprotect 收紧/放宽文件共享视图(MAP_SHARED)
// 文件 16 字节 "AAAABBBBCCCCDDDD",O_RDWR + MAP_SHARED + RW:
//   1) 写一次成功(第二个 fd 的 pread 立刻可见 —— 页缓存)
//   2) mprotect 降为 PROT_READ,再写 → SIGSEGV(SEGV_ACCERR)
//   3) mprotect 升回 RW,又能写
//   4) mprotect 地址不按页对齐 → EINVAL
//   5) 顺手验证文章的断言:O_RDONLY fd + PROT_WRITE + MAP_SHARED → EACCES;
//      同一 fd 换 MAP_PRIVATE → 合法(动态链接器那一手)
#include "article.hpp"
#include "sigout.hpp"

#include <print>
#include <cstring>
#include <string>
#include <sys/wait.h>

namespace {

volatile sig_atomic_t g_which = 0; // 同 E1:场景号必须 volatile,防 -O2 重排

void on_sigsegv(int, siginfo_t* info, void*)
{
    write_all("\n[handler] SIGSEGV, si_addr = ");
    write_hex(reinterpret_cast<std::uintptr_t>(info->si_addr));
    write_all(info->si_code == SEGV_ACCERR
                  ? ", si_code = SEGV_ACCERR(2)\n" : ", si_code = other\n");
    ::_exit(70 + g_which);
}

std::string peek(unique_fd& fd)
{
    char buf[16] {};
    sys_call("pread", ::pread, fd.get(), buf, sizeof buf, 0);
    return std::string{buf, sizeof buf};
}

} // namespace

int main()
{
    const char* path = "/tmp/l02_exps/e2_mprotect_file/data.bin";
    {
        unique_fd w{sys_call("open", ::open, path, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
        sys_call("write", ::write, w.get(), "AAAABBBBCCCCDDDD", 16);
    }
    unique_fd fd{sys_call("open", ::open, path, O_RDWR)};
    unique_fd fd2{sys_call("open", ::open, path, O_RDONLY)}; // 独立的打开文件描述,只读旁观

    mapped_region region(fd, 16, PROT_READ | PROT_WRITE, MAP_SHARED);
    std::print("mapped 16 bytes (rounded to {} B) at 0x{:x}, MAP_SHARED RW\n",
               region.size(), reinterpret_cast<std::uintptr_t>(region.data()));

    // 1) RW 阶段:写成功,pread 立刻可见
    std::memcpy(region.data(), "XXXX", 4);
    std::print("1) write 'XXXX' as RW      : ok,  fd2 pread = {}\n", peek(fd2));

    // 2) 收紧:整段降为只读,子进程里再写
    sys_call("mprotect->READ", ::mprotect, region.data(), region.size(), PROT_READ);
    std::print("2) mprotect -> PROT_READ   : ok,  view is now read-only\n");
    std::fflush(stdout);
    pid_t pid = ::fork();
    if (pid == 0) {
        struct sigaction sa {};
        sa.sa_sigaction = on_sigsegv;
        sa.sa_flags = SA_SIGINFO;
        ::sigaction(SIGSEGV, &sa, nullptr);
        g_which = 1;                            // 把场景号写进 volatile 变量,不许被重排
        volatile unsigned char* vp = region.data();
        *vp = 'Y';                              // 只读视图上写 → SIGSEGV(volatile 写,不许被重排)
        ::_exit(0);
    }
    int st = 0;
    ::waitpid(pid, &st, 0);
    std::print("   parent: child exit status = {} (WIFEXITED={}, code={})\n",
               st, WIFEXITED(st), WEXITSTATUS(st));

    // 3) 放宽:升回 RW,又能写,pread 交叉验证
    sys_call("mprotect->RW", ::mprotect, region.data(), region.size(), PROT_READ | PROT_WRITE);
    std::memcpy(region.data(), "ZZZZ", 4);
    std::print("3) mprotect -> RW again    : ok,  write 'ZZZZ', fd2 pread = {}\n", peek(fd2));

    // 4) 地址不按页对齐
    errno = 0;
    int rc = ::mprotect(region.data() + 8, 4096, PROT_READ);
    std::print("4) mprotect(base+8, ...)   : rc={}, errno={} ({})  <- addr 必须页对齐\n",
               rc, errno, std::strerror(errno));

    // 5) prot 与 open 模式打架:只读 fd + PROT_WRITE + MAP_SHARED
    errno = 0;
    void* p = ::mmap(nullptr, 16, PROT_WRITE, MAP_SHARED, fd2.get(), 0);
    std::print("5) O_RDONLY fd + PROT_WRITE + MAP_SHARED : {}",
               p == MAP_FAILED ? "MAP_FAILED" : "unexpected success");
    if (p == MAP_FAILED) std::print(", errno={} ({})\n", errno, std::strerror(errno));
    errno = 0;
    p = ::mmap(nullptr, 16, PROT_WRITE, MAP_PRIVATE, fd2.get(), 0);
    std::print("   same fd, switch to MAP_PRIVATE         : {}",
               p == MAP_FAILED ? "MAP_FAILED" : "mapped (COW, legal)");
    if (p == MAP_FAILED) std::print(", errno={} ({})\n", errno, std::strerror(errno));
    else { std::print("\n"); ::munmap(p, 4096); }

    return 0;
}
