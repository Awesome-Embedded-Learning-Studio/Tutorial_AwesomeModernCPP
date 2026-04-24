// Test: CTAD 基本功能验证
// 验证标准库容器的 CTAD 支持

#include <utility>
#include <tuple>
#include <vector>
#include <array>
#include <optional>
#include <mutex>
#include <iostream>
#include <type_traits>

void test_pair_tuple_ctad() {
    std::pair p(1, 2.0);
    static_assert(std::is_same_v<decltype(p), std::pair<int, double>>);

    std::pair p2 = {1, 2.0};
    static_assert(std::is_same_v<decltype(p2), std::pair<int, double>>);

    std::tuple t(1, 2.0, "three");

    std::cout << "pair/tuple CTAD works\n";
}

void test_vector_array_ctad() {
    std::vector v = {1, 2, 3};
    static_assert(std::is_same_v<decltype(v), std::vector<int>>);

    std::array a = {1, 2, 3, 4, 5};
    static_assert(std::is_same_v<decltype(a), std::array<int, 5>>);

    std::cout << "vector/array CTAD works\n";
}

void test_optional_ctad() {
    std::optional o = 42;
    static_assert(std::is_same_v<decltype(o), std::optional<int>>);

    std::cout << "optional CTAD works\n";
}

void test_lock_guard_ctad() {
    std::mutex mtx;
    std::lock_guard lock(mtx);

    std::cout << "lock_guard CTAD works\n";
}

int main() {
    test_pair_tuple_ctad();
    test_vector_array_ctad();
    test_optional_ctad();
    test_lock_guard_ctad();
    return 0;
}
