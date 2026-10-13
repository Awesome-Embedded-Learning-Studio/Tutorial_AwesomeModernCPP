#include <cerrno>
#include <cstdio>
#include <netdb.h>
static void probe(const char* what, const char* host, int flags) {
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = flags;
    struct addrinfo* res = nullptr;
    int g = getaddrinfo(host, nullptr, &hints, &res);
    std::printf("%-28s rc=%2d errno after=%d\n", what, g, errno);
    if (g == 0)
        freeaddrinfo(res);
}
int main() {
    errno = 0;
    probe("numeric AI_NUMERICHOST", "127.0.0.1", AI_NUMERICHOST);
    errno = 0;
    probe("plain numeric (no flag)", "127.0.0.1", 0);
    errno = 0;
    probe("name \"localhost\"", "localhost", 0);
    errno = 0;
    probe("name again (cache warm)", "localhost", 0);
}
