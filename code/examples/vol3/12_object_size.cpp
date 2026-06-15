// Object size, alignment, trivial types and aggregate initialization
#include <cstdint>
#include <iostream>
#include <type_traits>

// Padding demo: member order affects size
struct A {
    char c; // 1 byte
    int i;  // 4 bytes, alignment 4
};
// typical: c@0, pad@1-3, i@4-7 => sizeof == 8

struct B {
    char a;
    int i;
    char b;
};
// typical: a@0, pad@1-3, i@4-7, b@8, pad@9-11 => sizeof == 12

struct C {
    char a;
    char b;
    int i;
};
// typical: a@0, b@1, pad@2-3, i@4-7 => sizeof == 8

struct alignas(16) Vec4 {
    float x, y, z, w;
};

// Trivial / trivially_copyable / standard-layout
struct S {
    int x;
    double y;
};

struct T {
    T() { /* user-provided ctor */ }
    int x;
};

// Aggregate initialization & C++20 designated init
struct DeviceConfig {
    uint32_t mode;
    uint32_t timeout_ms;
    uint8_t flags;
};

int main() {
    std::cout << "=== Padding Demo ===\n";
    std::cout << "sizeof(A) = " << sizeof(A) << "  (char + int)\n";
    std::cout << "sizeof(B) = " << sizeof(B) << "  (char + int + char)\n";
    std::cout << "sizeof(C) = " << sizeof(C) << "  (char + char + int)\n";
    std::cout << "sizeof(Vec4) = " << sizeof(Vec4) << "  alignof = " << alignof(Vec4) << '\n';

    std::cout << "\n=== Type Traits ===\n";
    static_assert(std::is_trivially_copyable_v<S>);
    static_assert(std::is_standard_layout_v<S>);
    std::cout << "S is trivially_copyable: " << std::is_trivially_copyable_v<S> << '\n';
    std::cout << "S is standard_layout:   " << std::is_standard_layout_v<S> << '\n';

    // T is NOT trivial (user-provided ctor)
    static_assert(!std::is_trivial_v<T>);
    std::cout << "T is trivial:            " << std::is_trivial_v<T> << '\n';

    std::cout << "\n=== Aggregate Init (C++20 designated) ===\n";
    DeviceConfig cfg{
        .mode = 3,
        .timeout_ms = 1000,
        // .flags omitted -> zero-initialized
    };
    std::cout << "mode=" << cfg.mode << " timeout=" << cfg.timeout_ms
              << " flags=" << static_cast<int>(cfg.flags) << '\n';

    // Nested designated init
    struct Header {
        uint16_t id;
        uint16_t flags;
    };
    struct Packet {
        Header hdr;
        uint8_t payload[4];
    };

    Packet pkt{.hdr = {.id = 0x1234, .flags = 0x1}, .payload = {0xAA, 0x00, 0x00, 0x55}};
    std::cout << "Packet id=0x" << std::hex << pkt.hdr.id << " flags=0x" << pkt.hdr.flags
              << std::dec << '\n';

    return 0;
}
