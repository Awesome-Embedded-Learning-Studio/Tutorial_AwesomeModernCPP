// fixed_vector<T, N>:编译期定长、连续存储、零动态分配的 vector
// 对应文章:documents/vol4-advanced/vol1-basics-cpp11-14/10-fixed-vector.md
// 编译:g++ -Wall -Wextra -std=c++20 fixed_vector.cpp -o fixed_vector && ./fixed_vector
#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>

template <typename T, std::size_t N> class FixedVector {
    std::array<T, N> data_{};
    std::size_t size_ = 0;

  public:
    static constexpr std::size_t capacity_v = N;

    constexpr void push_back(const T& value) {
        if (size_ >= N)
            throw std::out_of_range("FixedVector full");
        data_[size_++] = value;
    }
    constexpr T& operator[](std::size_t i) { return data_[i]; }
    constexpr const T& operator[](std::size_t i) const { return data_[i]; }
    constexpr std::size_t size() const { return size_; }

    // 裸指针当迭代器:元素连续存储,T* 天生满足随机访问迭代器要求
    constexpr T* begin() { return data_.data(); }
    constexpr T* end() { return data_.data() + size_; }
    constexpr const T* begin() const { return data_.data(); }
    constexpr const T* end() const { return data_.data() + size_; }
};

int main() {
    FixedVector<int, 8> v;
    for (int i = 1; i <= 5; ++i)
        v.push_back(i * 10);

    std::cout << "size = " << v.size() << " capacity = " << decltype(v)::capacity_v << "\n";
    std::cout << "elements: ";
    for (auto x : v)
        std::cout << x << " ";
    std::cout << "\n";
    std::cout << "v[2] = " << v[2] << "\n";
    std::cout << "sizeof(FixedVector<int,8>) = " << sizeof(FixedVector<int, 8>) << "\n";
    std::cout << "sizeof(int*) = " << sizeof(int*) << " (动态 vector 至少含 3 个指针 + 堆分配)\n";
    return 0;
}
