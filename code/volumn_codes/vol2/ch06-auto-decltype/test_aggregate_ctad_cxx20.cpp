// Test: C++20 聚合 CTAD 验证
// 验证 C++20 是否支持聚合类型的 CTAD

#include <cstddef>

template<typename T, std::size_t N>
struct MyArray {
    T data[N];
};

int main() {
    // 这个在 C++20 中应该能工作
    MyArray a = {1, 2, 3};

    return 0;
}
