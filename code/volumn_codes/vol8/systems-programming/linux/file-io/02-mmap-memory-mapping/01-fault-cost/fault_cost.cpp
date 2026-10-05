// 复核:fault_cost(文章「缺页」一节)——64 MiB 文件,三趟每页摸一字节
// 输出格式与文章一致;sum 取决于文件内容模式,量级/计数才是对账目标。
#include "article.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

unsigned long self_minflt() // /proc/self/stat 第 10 字段(comm 可能带空格,从 ')' 后数)
{
    std::ifstream f{"/proc/self/stat"};
    std::string s;
    std::getline(f, s);
    const auto rp = s.rfind(')');
    std::istringstream is{s.substr(rp + 2)};
    std::string tok;
    for (int field = 3; field <= 10; ++field) {
        is >> tok;
    }
    return std::strtoul(tok.c_str(), nullptr, 10);
}

unsigned long touch_pages(const volatile unsigned char* data, std::size_t size, std::size_t page)
{
    unsigned long sum = 0;
    for (std::size_t i = 0; i < size; i += page) {
        sum += data[i];
    }
    return sum;
}

} // namespace

int main()
{
    const char* path = "/tmp/l02_exps/recheck/fault64.bin";
    const std::size_t page = static_cast<std::size_t>(::sysconf(_SC_PAGE_SIZE));
    constexpr std::size_t kLen = 64u << 20;

    { // 造 64 MiB 文件,内容确定(模式与文章的未必相同,sum 会不同)
        unique_fd w{sys_call("open", ::open, path, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
        std::vector<unsigned char> chunk(page);
        for (std::size_t i = 0; i < kLen; i += page) {
            for (std::size_t j = 0; j < page; ++j) {
                chunk[j] = static_cast<unsigned char>((i + j) * 2654435761u >> 13);
            }
            sys_call("write", ::write, w.get(), chunk.data(), chunk.size());
        }
    }

    unique_fd fd{sys_call("open", ::open, path, O_RDONLY)};
    mapped_region region(fd, kLen, PROT_READ, MAP_PRIVATE);

    std::printf("page size     : %zu, mapped %zu pages\n", page, region.size() / page);

    unsigned long f0 = self_minflt();
    auto t0 = std::chrono::steady_clock::now();
    unsigned long s1 = touch_pages(region.data(), region.size(), page); // 第一趟,缺页
    auto t1 = std::chrono::steady_clock::now();
    unsigned long f1 = self_minflt();
    unsigned long s2 = touch_pages(region.data(), region.size(), page); // 第二趟,页都在
    auto t2 = std::chrono::steady_clock::now();
    unsigned long f2 = self_minflt();
    ::madvise(region.data(), region.size(), MADV_DONTNEED);             // 丢页,人造第三次首次
    unsigned long s3 = touch_pages(region.data(), region.size(), page); // 缺页应声回来
    auto t3 = std::chrono::steady_clock::now();
    unsigned long f3 = self_minflt();
    (void)f0;

    using dns = std::chrono::duration<double, std::milli>;
    std::printf("first touch   : %7.2f ms, minor faults %5lu, sum %lu\n", dns(t1 - t0).count(), f1 - f0, s1);
    std::printf("second touch  : %7.2f ms, minor faults %5lu, sum %lu\n", dns(t2 - t1).count(), f2 - f1, s2);
    std::printf("after DONTNEED: %7.2f ms, minor faults %5lu, sum %lu\n", dns(t3 - t2).count(), f3 - f2, s3);
    return 0;
}
