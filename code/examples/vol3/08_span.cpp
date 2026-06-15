// std::span: lightweight non-owning view demo (C++20)
#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

void print_bytes(std::span<const uint8_t> s) {
    for (auto b : s) {
        std::cout << std::hex << static_cast<int>(b) << ' ';
    }
    std::cout << std::dec << '\n';
}

int main() {
    // Construct from different container types
    uint8_t buffer[] = {0x10, 0x20, 0x30};
    std::vector<uint8_t> v = {1, 2, 3, 4};
    std::array<uint8_t, 3> a = {9, 8, 7};

    std::cout << "C array:  ";
    print_bytes(buffer);

    std::cout << "vector:   ";
    print_bytes(v);

    std::cout << "array:    ";
    print_bytes(a);

    // Subview: first(2) elements from vector
    std::cout << "first 2:  ";
    print_bytes({v.data(), 2});

    // Dynamic extent vs static extent
    int arr[4] = {10, 20, 30, 40};
    std::span<int, 4> s_fixed(arr); // static extent
    std::span<int> s_dyn(arr);      // dynamic extent

    std::cout << std::dec << "fixed size: " << s_fixed.size() << '\n'
              << "dyn size:   " << s_dyn.size() << '\n'
              << "size_bytes: " << s_dyn.size_bytes() << '\n';

    // Subspan for protocol parsing
    std::span<uint8_t> packet(reinterpret_cast<uint8_t*>(arr), 4);
    auto hdr = packet.first(2);
    std::cout << "header: " << static_cast<int>(hdr[0]) << ',' << static_cast<int>(hdr[1]) << '\n';

    return 0;
}
