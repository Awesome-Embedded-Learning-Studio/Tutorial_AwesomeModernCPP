// 配套 04-ctad.md「手写推导指南:为什么 array 的 N 能推出来」
// 演示 std::array 的 CTAD,以及仿写一个 array 风格的推导指南
// 编译运行:g++ -std=c++17 -Wall -Wextra array_ctad.cpp -o array_ctad && ./array_ctad
#include <array>
#include <cstddef>
#include <iostream>
#include <type_traits>

// 标准库的 array CTAD:元素类型和大小都从花括号列表推
void std_array_demo() {
    std::array a{1, 2, 3}; // array<int, 3>
    static_assert(std::is_same_v<decltype(a), std::array<int, 3>>);
    static_assert(a.size() == 3);

    std::array b{1.0, 2.0}; // array<double, 2>
    static_assert(std::is_same_v<decltype(b), std::array<double, 2>>);

    std::cout << "std::array a: size=" << a.size() << "  [0]=" << a[0] << "  [2]=" << a[2] << "\n";
}

// 仿写简化版 array:N 没法从构造参数推,得靠手写指南
template <typename T, std::size_t N> struct MyArray {
    T data[N];
    MyArray(const T (&arr)[N]) {
        for (std::size_t i = 0; i < N; ++i)
            data[i] = arr[i];
    }
};

// 关键:手写一条「从 C 数组反推 N」的推导指南
template <typename U, std::size_t N> MyArray(const U (&)[N]) -> MyArray<U, N>;

void custom_array_demo() {
    int raw[] = {1, 2, 3, 4};
    MyArray ma(raw); // 指南出手:MyArray<int, 4>
    static_assert(std::is_same_v<decltype(ma), MyArray<int, 4>>);

    double raw2[] = {1.5, 2.5};
    MyArray mb(raw2); // MyArray<double, 2>
    static_assert(std::is_same_v<decltype(mb), MyArray<double, 2>>);

    std::cout << "MyArray ma: ";
    for (std::size_t i = 0; i < 4; ++i)
        std::cout << ma.data[i] << " ";
    std::cout << "\n";
}

int main() {
    std_array_demo();
    custom_array_demo();
    return 0;
}
