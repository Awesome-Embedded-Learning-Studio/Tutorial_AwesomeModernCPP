// array_vs_carray_host.cpp
// std::array 与 C 数组的零开销对比

#include <array>
#include <cstdint>
#include <iostream>

void size_comparison() {
    int raw[4] = {1, 2, 3, 4};
    std::array<int, 4> arr = {1, 2, 3, 4};

    std::cout << "sizeof(raw) = " << sizeof(raw) << "\n";
    std::cout << "sizeof(arr) = " << sizeof(arr) << "\n";
    std::cout << "Zero overhead? " << (sizeof(raw) == sizeof(arr) ? "yes" : "no") << "\n";
}

void pass_raw(int ptr[]) {
    std::cout << "Inside function: sizeof(ptr) = " << sizeof(ptr) << " (decayed!)\n";
}

void pass_std_array(const std::array<int, 4>& arr) {
    std::cout << "Inside function: arr.size() = " << arr.size() << " (safe!)\n";
}

constexpr std::array<uint16_t, 8> crc_table = [] {
    std::array<uint16_t, 8> t{};
    for (std::size_t i = 0; i < t.size(); ++i)
        t[i] = static_cast<uint16_t>(i * 0x1021);
    return t;
}();

int main() {
    std::cout << "=== std::array vs C array ===\n\n";
    size_comparison();

    std::cout << "\n--- Decay problem ---\n";
    int raw[4] = {10, 20, 30, 40};
    std::array<int, 4> arr = {10, 20, 30, 40};
    std::cout << "Caller: sizeof(raw) = " << sizeof(raw) << "\n";
    pass_raw(raw);
    pass_std_array(arr);

    std::cout << "\n--- Compile-time table ---\n";
    for (std::size_t i = 0; i < crc_table.size(); ++i) {
        std::cout << "crc_table[" << i << "] = 0x" << std::hex << crc_table[i] << std::dec << "\n";
    }
    static_assert(crc_table.size() == 8);

    return 0;
}
