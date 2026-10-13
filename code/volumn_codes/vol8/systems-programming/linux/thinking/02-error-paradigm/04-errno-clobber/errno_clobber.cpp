// E4:errno 读取时机——失败之后、读取之前,中间干的那件事会不会把 errno 冲掉?
// 编译:g++ -std=c++20 -Wall -Wextra e4_errno_clobber.cpp -o e4
//
// 跑法:./e4 enoent   基线失败 = open 不存在路径(基线值 ENOENT=2)
//      ./e4 ebadf   基线失败 = write 已关 fd(基线值 EBADF=9)
//
// 每个场景独立三步:造基线失败 -> 执行「插入操作」 -> 立刻读 errno,与基线比对。
// 值变了 = 被污染。两条基线都跑一遍,可以识破「污染了但恰好等于基线值」的盲区。
// 场景 11(stderr 已被关闭)放在最后,它会把 fd 2 关掉,之后的 stderr 输出全部丢失。
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <string>
#include <unistd.h>

static int make_baseline_failure(const char* mode) {
    if (std::strcmp(mode, "ebadf") == 0) {
        int f = ::open("/dev/null", O_WRONLY);
        ::close(f);
        ssize_t r = ::write(f, "x", 1); // -1/EBADF
        (void)r;
    } else {
        int f = ::open("/tmp/errpar/e4_definitely_missing", O_RDONLY); // -1/ENOENT
        if (f != -1)
            ::close(f);
    }
    return errno;
}

struct Probe {
    const char* name;
    void (*fn)();
};

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "enoent";
    // stdout 行缓冲,让重定向到文件时与 stderr 的交错可读
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    static const Probe probes[] = {
        {"fprintf(stderr, ...)", [] { std::fprintf(stderr, "    (insert: fprintf to stderr)\n"); }},
        {"successful write(2, ...)",
         [] {
             ssize_t w = ::write(STDERR_FILENO, "    (insert: raw write to fd 2)\n", 32);
             (void)w;
         }},
        {"strerror(ENOMEM)",
         [] {
             const char* s = std::strerror(ENOMEM);
             (void)s;
         }},
        {"printf to stdout (buffered)",
         [] { std::printf("    (insert: buffered printf, no syscall while buffered)\n"); }},
        {"std::cout << (iostreams)", [] { std::cout << "    (insert: iostream line)\n"; }},
        {"malloc 1 MiB + free",
         [] {
             void* p = std::malloc(1024 * 1024); // 大块走 mmap
             std::free(p);
         }},
        {"std::string 4 KiB (heap)",
         [] {
             std::string s(4096, 'x');
             (void)s;
         }},
        {"fopen(\"/tmp\") + fclose",
         [] {
             FILE* f = std::fopen("/tmp", "r");
             if (f)
                 std::fclose(f);
         }},
        {"getaddrinfo(\"localhost\") ok",
         [] {
             struct addrinfo hints{};
             hints.ai_family = AF_UNSPEC;
             struct addrinfo* res = nullptr;
             if (getaddrinfo("localhost", nullptr, &hints, &res) == 0)
                 freeaddrinfo(res);
         }},
        {"SECOND FAILING open()",
         [] {
             int f = ::open("/tmp/errpar/e4_other_missing", O_RDONLY);
             if (f != -1)
                 ::close(f);
         }},
        {"fprintf(stderr) with fd 2 CLOSED",
         [] {
             ::close(STDERR_FILENO); // 之后 stderr 全部丢失,故放在最后一个
             std::fprintf(stderr, "this line goes nowhere\n");
         }},
    };

    std::printf("baseline mode = %s; probes = %zu\n", mode, sizeof probes / sizeof probes[0]);
    int polluted = 0;
    for (const Probe& p : probes) {
        int before = make_baseline_failure(mode);
        p.fn();
        int after = errno;
        bool hit = (after != before);
        polluted += hit;
        std::printf("[%2d] %-36s baseline=%2d after=%2d  %s\n", (int)(&p - probes), p.name, before,
                    after, hit ? "POLLUTED" : "unchanged");
    }
    std::printf("summary: %d of %zu probes polluted errno in this run\n", polluted,
                sizeof probes / sizeof probes[0]);
    return 0;
}
