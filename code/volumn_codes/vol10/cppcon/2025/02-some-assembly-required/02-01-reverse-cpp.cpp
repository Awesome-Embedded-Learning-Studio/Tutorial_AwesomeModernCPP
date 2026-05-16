#include <cstdint>
#include <cstdio>
#include <utility>

void reverse_bytes_cpp(void* data, size_t len) {
    if (len == 0) return;
    auto* bytes = static_cast<uint8_t*>(data);
    size_t left = 0;
    size_t right = len - 1;
    while (left < right) {
        std::swap(bytes[left], bytes[right]);
        ++left; --right;
    }
}

int main() {
    uint8_t buf[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    size_t len = sizeof(buf);
    printf("Before: ");
    for (size_t i = 0; i < len; i++) printf("%02x ", buf[i]);
    printf("\n");
    reverse_bytes_cpp(buf, len);
    printf("After:  ");
    for (size_t i = 0; i < len; i++) printf("%02x ", buf[i]);
    printf("\n");
    return 0;
}
