// E1:errno 装箱(errno -> std::error_code/generic_category)与线程局部性
// 编译:g++ -std=c++20 -Wall -Wextra -pthread e1_errno_boxing.cpp -o e1
// 环境:WSL2 内核 6.18.33.2-microsoft-standard-WSL2,g++ 16.2.1,AMD Ryzen 7 9700X
//
// 看三个证据:
//   [1] open 不存在路径 -> errno_code().value()==ENOENT,message() 可读
//   [2] write 到已关 fd -> errno_code().value()==EBADF
//   [3] 两线程各自制造不同失败,用 atomic flag 编排交错,
//       交错完成后各自的 errno_code() 仍取到各自的值(errno 是线程局部的)
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <system_error>
#include <thread>
#include <unistd.h>

// 公共工具(系列沿用):失败后立刻调用,errno 在这一刻定格进 error_code
std::error_code errno_code() noexcept {
    return std::error_code{errno, std::generic_category()};
}

int main() {
    // --- [1] ENOENT:open 不存在的路径 ---
    int fd = ::open("/tmp/errpar/no_such_file_e1", O_RDONLY);
    if (fd == -1) {
        std::error_code ec = errno_code();
        std::printf("[1] open(no_such_file_e1) failed:\n");
        std::printf("    ec.value() = %d   (ENOENT = %d)   equal = %s\n", ec.value(), ENOENT,
                    ec.value() == ENOENT ? "yes" : "NO");
        std::printf("    ec.message() = \"%s\"\n", ec.message().c_str());
        std::printf("    ec == std::errc::no_such_file_or_directory : %s\n",
                    ec == std::errc::no_such_file_or_directory ? "true" : "false");
    }

    // --- [2] EBADF:对已关 fd write ---
    int devnull = ::open("/dev/null", O_WRONLY);
    ::close(devnull);
    ssize_t w = ::write(devnull, "x", 1);
    std::error_code ec2 = errno_code();
    std::printf("[2] write to closed fd: return = %zd (expect -1)\n", w);
    std::printf("    ec2.value() = %d   (EBADF = %d)   equal = %s\n", ec2.value(), EBADF,
                ec2.value() == EBADF ? "yes" : "NO");
    std::printf("    ec2.message() = \"%s\"\n", ec2.message().c_str());

    // --- [3] 线程局部性:A 失败 -> B 失败 -> 双方再各自取值 ---
    // 若 errno 是"一个全局变量",A 在 B 失败之后取值应当看到 B 的 EBADF
    std::printf("[3] two threads, interleaved failures:\n");
    std::atomic<bool> a_failed{false};
    std::atomic<bool> b_failed{false};

    std::thread ta([&] {
        int f = ::open("/tmp/errpar/no_such_file_A", O_RDONLY); // 本线程 errno = ENOENT
        (void)f;
        a_failed.store(true, std::memory_order_release);
        while (!b_failed.load(std::memory_order_acquire)) { // 等 B 也失败完
        }
        std::error_code ec = errno_code(); // 交错之后才取
        std::printf("    [thread A] value = %d  (expect ENOENT=%d) -> %s\n", ec.value(), ENOENT,
                    ec.value() == ENOENT ? "kept own value" : "POLLUTED");
    });
    std::thread tb([&] {
        while (!a_failed.load(std::memory_order_acquire)) { // 等 A 先失败
        }
        int f = ::open("/dev/null", O_WRONLY);
        ::close(f);
        ssize_t r = ::write(f, "x", 1); // 本线程 errno = EBADF
        (void)r;
        b_failed.store(true, std::memory_order_release);
        std::error_code ec = errno_code(); // 交错之后才取
        std::printf("    [thread B] value = %d  (expect EBADF=%d) -> %s\n", ec.value(), EBADF,
                    ec.value() == EBADF ? "kept own value" : "POLLUTED");
    });
    ta.join();
    tb.join();
    std::printf("    (interleave: A fail -> B fail -> both errno_code(); ");
    std::printf("a plain global would cross-contaminate here)\n");
    return 0;
}
