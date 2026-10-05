// 01-strace/strace_demo.cpp —— E1:RAII 析构在 syscall 层的证据
//
// 三段作用域:正常离开 / 提前 return 离开 / 映射离开。
// 配套 strace(见本目录 README 与 .out 存档):
//   strace -f -e trace=openat,close   ./strace_demo   # 看 close
//   strace -f -e trace=mmap,munmap    ./strace_demo   # 看 munmap
//
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I common 01-strace/strace_demo.cpp

#include "raii.hpp"

#include <cstdio>
#include <cstring>

static const char kPath[] = "/tmp/raii_lab/01-strace/note.txt";

static void write_all(int fd, const char* data) {
    std::size_t len = std::strlen(data);
    while (len > 0) {
        ssize_t n = sys_call("write", ::write, fd, data, len);
        data += n;
        len -= static_cast<std::size_t>(n);
    }
}

// 提前 return 的典型泄漏现场:三条错误分支各写一个 close 的活,这里交给析构
static bool early_return_demo() {
    unique_fd fd{sys_call("open", ::open, kPath, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
    write_all(fd.get(), "written just before the early return\n");
    std::printf("[early-return] fd=%d still open, returning now\n", fd.get());
    std::fflush(stdout);
    return false; // 不写 close,~unique_fd() 在栈展开时收尾
}

int main() {
    std::printf("== phase 0: create the file (first scope) ==\n");
    std::fflush(stdout);
    {
        unique_fd fd{sys_call("open", ::open, kPath, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
        write_all(fd.get(), "written just before the early return\n");
    }

    std::printf("== phase 1: normal scope exit ==\n");
    std::fflush(stdout);
    {
        unique_fd fd{sys_call("open", ::open, kPath, O_RDONLY)};
        std::printf("[scope] fd=%d in scope\n", fd.get());
        std::fflush(stdout);
    } // 离开作用域,~unique_fd() 里 close(fd_)

    std::printf("== phase 2: early return ==\n");
    std::fflush(stdout);
    early_return_demo();

    std::printf("== phase 3: mapped_region ==\n");
    std::fflush(stdout);
    {
        unique_fd fd{sys_call("open", ::open, kPath, O_RDONLY)};
        mapped_region region(fd, 16, PROT_READ, MAP_PRIVATE);
        std::printf("[map] asked 16 B, region claims %zu B at %p, first byte '%c'\n", region.size(),
                    static_cast<void*>(region.data()), region.data()[0]);
        std::fflush(stdout);
    } // ~mapped_region() 里 munmap,随后 ~unique_fd() 里 close

    std::printf("== main returns ==\n");
}
