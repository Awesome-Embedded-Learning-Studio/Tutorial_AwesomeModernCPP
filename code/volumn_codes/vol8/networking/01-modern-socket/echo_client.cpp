// Modern echo client:connect → write 一行 → read 回显。配合 01 的 server。
#include "unique_fd.hpp"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstdint>
#include <netinet/in.h>
#include <print>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char** argv) {
    constexpr std::uint16_t kPort = 13013;
    std::string msg = (argc > 1) ? argv[1] : "hello from modern client";

    int raw = ::socket(AF_INET, SOCK_STREAM, 0);
    if (raw < 0) {
        std::print(stderr, "socket errno {}\n", errno);
        return 1;
    }
    UniqueFd fd{raw};

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPort);
    if (::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0) {
        std::print(stderr, "inet_pton failed\n");
        return 1;
    }
    if (::connect(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::print(stderr, "connect errno {}\n", errno);
        return 1;
    }

    std::string_view s{msg};
    if (::write(fd.get(), s.data(), s.size()) < 0) {
        std::print(stderr, "write errno {}\n", errno);
        return 1;
    }

    std::array<char, 4096> buf;
    ssize_t n = ::read(fd.get(), buf.data(), buf.size());
    if (n > 0)
        std::print("echo <- '{}'\n", std::string_view{buf.data(), static_cast<std::size_t>(n)});
    else
        std::print("no reply (read returned {})\n", n);
}
