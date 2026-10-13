// 02-leak/leak_raii.cpp —— E2 实验组:unique_fd 接管,异常路径一个不漏
//
// 用法:./leak_raii [N](默认 1000)。与 leak_raw 唯一的差别:
// try 块里 open 出来的 fd 交给 unique_fd,catch 之前栈展开时析构已 close。
//
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I common 02-leak/leak_raii.cpp

#include "procfd.hpp"
#include "raii.hpp"

#include <cstdio>
#include <stdexcept>

#include <sys/resource.h>

static const char kPath[] = "/tmp/raii_lab/02-leak/probe.bin";

int main(int argc, char** argv) {
    const int n = argc > 1 ? std::atoi(argv[1]) : 1000;

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
    for (int i = 0; i < n; ++i) {
        try {
            unique_fd fd{sys_call("open", ::open, kPath, O_RDONLY)};
            throw std::runtime_error("error path leaves the scope now");
        } catch (const std::exception&) {
            ++caught; // fd 已经在栈展开时被 ~unique_fd() close 了
        }
    }

    const auto after = list_open_fds();
    print_fds("fds after ", after);
    std::printf("loop=%d caught=%d leaked=%lld\n", n, caught,
                static_cast<long long>(after.size()) - static_cast<long long>(before.size()));
}
