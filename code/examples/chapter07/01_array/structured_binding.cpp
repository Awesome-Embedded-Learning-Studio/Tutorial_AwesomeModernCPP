// structured_binding.cpp - 演示 std::get 和结构化绑定
#include <array>
#include <iostream>
#include <utility>  // for std::tuple_size, std::tuple_element
#include <type_traits>  // for std::is_same_v

// 编译期索引访问（模板元编程友好）
template<size_t I>
int get_element(const std::array<int, 3>& arr) {
    return std::get<I>(arr);
}

// 结构化绑定用于解构返回值
auto get_coordinates() -> std::array<double, 3> {
    return {1.5, 2.5, 3.5};
}

int main() {
    // 基本结构化绑定 (C++17)
    std::array<int, 3> a = {1, 2, 3};
    auto [x, y, z] = a;
    std::cout << "x=" << x << ", y=" << y << ", z=" << z << '\n';

    // std::get<I>(array) 访问
    std::cout << "std::get<0>(a) = " << std::get<0>(a) << '\n';
    std::cout << "std::get<1>(a) = " << std::get<1>(a) << '\n';
    std::cout << "std::get<2>(a) = " << std::get<2>(a) << '\n';

    std::cout << "get_element<0>(a) = " << get_element<0>(a) << '\n';

    // tuple_size 和 tuple_element 支持
    using ArrayType = std::array<int, 3>;
    constexpr size_t size = std::tuple_size_v<ArrayType>;
    using ElementType = std::tuple_element_t<0, ArrayType>;

    std::cout << "Array size (via tuple_size): " << size << '\n';
    std::cout << "Element type is int: " << std::is_same_v<ElementType, int> << '\n';

    auto [px, py, pz] = get_coordinates();
    std::cout << "Point: (" << px << ", " << py << ", " << pz << ")\n";

    return 0;
}
