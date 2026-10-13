// 复核:sigbus(文章「SIGBUS」一节)——映射两页,ftruncate 砍半,摸第二页
#include "article.hpp"
#include "sigout.hpp"

#include <signal.h>

#include <cstdio>
#include <vector>

namespace {

void on_sigbus(int, siginfo_t* info, void*)
{
    write_all("\n[handler] caught SIGBUS, faulting address = ");
    write_hex(reinterpret_cast<std::uintptr_t>(info->si_addr));
    write_all(info->si_code == BUS_ADRERR ? ", si_code = BUS_ADRERR\n" : "\n");
    write_all("[handler] signal, not a return value; _exit(70)\n");
    ::_exit(70);
}

} // namespace

int main()
{
    const char* path = "/tmp/l02_exps/recheck/sigbus.bin";
    const long page = ::sysconf(_SC_PAGE_SIZE);
    {
        unique_fd w{sys_call("open", ::open, path, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
        std::vector<char> blob(static_cast<std::size_t>(2 * page), 'S');
        sys_call("write", ::write, w.get(), blob.data(), blob.size());
    }
    unique_fd fd{sys_call("open", ::open, path, O_RDWR)};

    mapped_region region(fd, static_cast<std::size_t>(2 * page), PROT_READ, MAP_SHARED);
    std::printf("mapped 2 pages (%zu bytes) at 0x%zx\n", region.size(),
                reinterpret_cast<std::uintptr_t>(region.data()));

    struct sigaction sa {};
    sa.sa_sigaction = on_sigbus;
    sa.sa_flags = SA_SIGINFO;
    ::sigaction(SIGBUS, &sa, nullptr);

    volatile unsigned char first = region.data()[0];    // 页内,安全
    (void)first;
    std::printf("touch +0        ... ok\n");

    sys_call("ftruncate", ::ftruncate, fd.get(), page); // 文件砍半,映射毫不知情
    std::printf("ftruncate -> %ld bytes, mapping still claims %zu\n", page, region.size());

    std::printf("touch +%-6ld    ... beyond EOF\n", page);
    std::fflush(stdout);
    volatile unsigned char second = region.data()[page]; // 越界,SIGBUS 在此爆发
    (void)second;
    std::printf("NOT REACHED\n");
    return 0;
}
