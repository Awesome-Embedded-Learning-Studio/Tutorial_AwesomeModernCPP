// ebo_arm.cpp
// ARM freestanding: 空基类优化 sizeof 对比

#include <cstddef>

struct Empty {};

struct AsMember {
    Empty e;
    int x;
};

struct AsBase : Empty {
    int x;
};

struct WithNoUniqueAddress {
    [[no_unique_address]] Empty e;
    int x;
};

extern "C" {
std::size_t size_empty() {
    return sizeof(Empty);
}
std::size_t size_as_member() {
    return sizeof(AsMember);
}
std::size_t size_as_base() {
    return sizeof(AsBase);
}
std::size_t size_no_unique() {
    return sizeof(WithNoUniqueAddress);
}
}
