// array_vs_carray_arm.cpp
// ARM freestanding: std::array 与 C 数组的零开销对比

#include <cstddef>
#include <cstdint>

int sum_raw(const int* data, std::size_t n) {
    int total = 0;
    for (std::size_t i = 0; i < n; ++i) {
        total += data[i];
    }
    return total;
}

template <std::size_t N> int sum_array(const int (&data)[N]) {
    int total = 0;
    for (std::size_t i = 0; i < N; ++i) {
        total += data[i];
    }
    return total;
}

extern "C" {
int test_raw() {
    int buf[4] = {1, 2, 3, 4};
    return sum_raw(buf, 4);
}
int test_array() {
    int buf[4] = {1, 2, 3, 4};
    return sum_array(buf);
}
}
