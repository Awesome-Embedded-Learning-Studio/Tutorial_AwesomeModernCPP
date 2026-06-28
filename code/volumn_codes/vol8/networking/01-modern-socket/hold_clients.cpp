// C10K 探测:打开 N 条空闲连接并 hold 一段时间。
// 配合读取 server 的 /proc/<pid>/status,实测"每连接一线程"模型在并发上去之后的
// 虚拟内存 / 线程数代价。配套 01 文章的 C10K 实测段。
#include "unique_fd.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <netinet/in.h>
#include <print>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

int main(int argc, char** argv) {
    const int n = (argc > 1) ? std::atoi(argv[1]) : 500;
    constexpr std::uint16_t kPort = 13013;

    std::vector<UniqueFd> conns;
    conns.reserve(static_cast<std::size_t>(n));
    int ok = 0;
    for (int i = 0; i < n; ++i) {
        int raw = ::socket(AF_INET, SOCK_STREAM, 0);
        if (raw < 0) {
            std::print(stderr, "socket failed at i={} (errno {})\n", i, errno);
            break;
        }
        UniqueFd fd{raw};
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kPort);
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (::connect(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            conns.push_back(std::move(fd));
            ++ok;
        }
    }
    std::print("opened {} idle connections ({} requested); holding 10s...\n", ok, n);
    std::this_thread::sleep_for(std::chrono::seconds(10));
    std::print("releasing\n");
}
