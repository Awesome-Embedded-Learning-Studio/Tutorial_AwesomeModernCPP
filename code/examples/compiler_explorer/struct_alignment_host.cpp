// struct_alignment_host.cpp
// 结构体成员排列对 sizeof 的影响：对齐、填充与空间优化

#include <cstddef>
#include <cstdint>
#include <iostream>

struct BadLayout {
    char c;    // offset 0, 1 byte
    int32_t x; // offset 4 (3 bytes padding!)
    char d;    // offset 8
};

struct GoodLayout {
    int32_t x; // offset 0
    char c;    // offset 4
    char d;    // offset 5
};

int main() {
    std::cout << "BadLayout:  sizeof=" << sizeof(BadLayout) << "  alignof=" << alignof(BadLayout)
              << "\n";
    std::cout << "  c offset=" << offsetof(BadLayout, c) << "  x offset=" << offsetof(BadLayout, x)
              << "  d offset=" << offsetof(BadLayout, d) << "\n";

    std::cout << "GoodLayout: sizeof=" << sizeof(GoodLayout) << "  alignof=" << alignof(GoodLayout)
              << "\n";
    std::cout << "  x offset=" << offsetof(GoodLayout, x)
              << "  c offset=" << offsetof(GoodLayout, c)
              << "  d offset=" << offsetof(GoodLayout, d) << "\n";

    std::cout << "Saved " << (sizeof(BadLayout) - sizeof(GoodLayout))
              << " bytes by reordering members.\n";
    return 0;
}
