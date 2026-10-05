// 02-leak/leak_raw.cpp —— E2 对照组:裸 int fd,异常路径每次漏一个
//
// 用法:./leak_raw [N](默认 1000)。纪律:先小批量(8)试跑,确认离
// RLIMIT_NOFILE 还远,再上 1000,别把进程撑爆。
//
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I common 02-leak/leak_raw.cpp

#include "procfd.hpp"
#include "raii.hpp"

#include <cstdio>
#include <stdexcept>

#include <sys/resource.h>

static const char kPath[] = "/tmp/raii_lab/02-leak/probe.bin";

int main(int argc, char** argv) {
    const int n = argc > 1 ? std::atoi(argv[1]) : 1000;

    // 探测文件:造一次,后面只读
    {
        unique_fd fd{sys_call("open", ::open, kPath, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
        sys_call("write", ::write, fd.get(), "x", 1);
    }

    struct rlimit rl{};
    ::getrlimit(RLIMIT_NOFILE, &rl);
    std::printf("RLIMIT_NOFILE: soft=%lld hard=%lld\n", static_cast<long long>(rl.rlim_cur),
                static_cast<long long>(rl.rlim_max));

    const auto before = list_open_fds();
    print_fds("fds before", before);

    int caught = 0;
    int open_failures = 0;
    for (int i = 0; i < n; ++i) {
        try {
            // 裸 fd:出错路径离开作用域,没有谁会 close 它
            int fd = ::open(kPath, O_RDONLY);
            if (fd == -1) {
                ++open_failures; // 例:漏到 RLIMIT_NOFILE 时 EMFILE(errno=24)
                continue;
            }
            (void)fd;
            throw std::runtime_error("error path leaves the scope now");
        } catch (const std::exception&) {
            ++caught; // catch 里也没有 close:fd 就这么留下了
        }
    }

    const auto after = list_open_fds();
    print_fds("fds after ", after);
    std::printf("loop=%d caught=%d open_failures=%d leaked=%lld\n", n, caught, open_failures,
                static_cast<long long>(after.size()) - static_cast<long long>(before.size()));
}
