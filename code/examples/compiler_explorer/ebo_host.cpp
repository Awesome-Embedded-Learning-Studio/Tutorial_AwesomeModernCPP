// ebo_host.cpp
// 空基类优化与 C++20 [[no_unique_address]]

#include <iostream>

struct Empty {};

struct AsMember {
    Empty e; // member: must occupy >= 1 byte
    int x;
};

struct AsBase : Empty { // EBO: Empty base costs 0 extra bytes
    int x;
};

#if __cplusplus >= 202002L
struct WithNoUniqueAddress {
    [[no_unique_address]] Empty e;
    int x;
};
#endif

int main() {
    std::cout << "sizeof(Empty)        = " << sizeof(Empty) << "\n";
    std::cout << "sizeof(AsMember)     = " << sizeof(AsMember) << " (Empty as member)\n";
    std::cout << "sizeof(AsBase)       = " << sizeof(AsBase) << " (Empty as base, EBO)\n";
#if __cplusplus >= 202002L
    std::cout << "sizeof(WithNoUnique) = " << sizeof(WithNoUniqueAddress)
              << " (C++20 [[no_unique_address]])\n";
#endif
    return 0;
}
