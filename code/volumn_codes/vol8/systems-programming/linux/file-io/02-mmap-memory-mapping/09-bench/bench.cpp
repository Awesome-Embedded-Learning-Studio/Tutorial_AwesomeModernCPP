// e6:512 MiB 顺序读 read vs mmap(笔记本轮,代码按文章 bench 节选复原)
// 节选注明「求和循环 sum_bytes 与计时打印略」,此处的 sum_bytes / 计时 / 打印
// 按节选注释与文中输出口径补齐;minor faults 读 /proc/self/stat 第 10 字段。
#include "../common/article.hpp"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <print>
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

unsigned long sum_bytes(const unsigned char* data, std::size_t size) // 求和循环,节选中略
{
    unsigned long sum = 0;
    for (std::size_t i = 0; i < size; ++i) {
        sum += data[i];
    }
    return sum;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::print("usage: {} read|mmap|populate <file>\n", argv[0]);
        return 1;
    }
    const std::string mode = argv[1];
    unique_fd fd{sys_call("open", ::open, argv[2], O_RDONLY)};

    ::lseek(fd.get(), 0, SEEK_END); // 量长度:lseek 到末尾
    const std::size_t len = static_cast<std::size_t>(::lseek(fd.get(), 0, SEEK_CUR));

    unsigned long sum = 0;
    const auto t0 = std::chrono::steady_clock::now();
    if (mode == "read") {
        ::lseek(fd.get(), 0, SEEK_SET); // 前面量长度时 lseek 到了末尾,读之前回到开头
        std::vector<unsigned char> buf(1u << 20); // 1 MiB 缓冲
        for (;;) {
            ssize_t n = ::read(fd.get(), buf.data(), buf.size()); // 页缓存→用户缓冲,一次拷贝
            if (n == 0) {
                break;
            }
            sum += sum_bytes(buf.data(), static_cast<std::size_t>(n));
        }
    } else {
        const int extra = (mode == "populate") ? MAP_POPULATE : 0;
        const auto tm0 = std::chrono::steady_clock::now();
        mapped_region region(fd, len, PROT_READ, MAP_PRIVATE | extra);
        const auto tm1 = std::chrono::steady_clock::now();
        if (extra) {
            std::print("mmap(MAP_POPULATE) itself took {:.1f} ms\n",
                       std::chrono::duration<double, std::milli>(tm1 - tm0).count());
        }
        sum = sum_bytes(region.data(), region.size()); // 首摸每页缺页,零拷贝
    }
    const auto t1 = std::chrono::steady_clock::now();

    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double mibs = static_cast<double>(len) / (1024.0 * 1024.0) / (ms / 1000.0);
    std::print("{:<9}: {:7.1f} ms, {:.1f} MiB/s, sum {}, minor faults {}\n",
               mode, ms, mibs, sum, self_minflt());
    return 0;
}
