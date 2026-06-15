// if constexpr: compile-time conditional compilation (C++17)
#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>

// Case 1: type-based dispatch
template <typename T> void print_value(const T& v) {
    if constexpr (std::is_integral_v<T>) {
        std::cout << "integral: " << v << "\n";
    } else if constexpr (std::is_floating_point_v<T>) {
        std::cout << "floating: " << v << "\n";
    } else {
        std::cout << "other:    " << v << "\n";
    }
}

// Case 2: detect member .size() with if constexpr
template <typename T, typename = void> constexpr bool has_size_v = false;

template <typename T>
constexpr bool has_size_v<T, std::void_t<decltype(std::declval<T>().size())>> = true;

template <typename T> auto get_size_if_possible(const T& t) {
    if constexpr (has_size_v<T>) {
        return t.size();
    } else {
        return std::size_t{0};
    }
}

// Case 3: compile-time factorial with if constexpr + constexpr function
constexpr uint64_t factorial(uint64_t n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

int main() {
    // Type dispatch
    print_value(42);
    print_value(3.14);
    print_value(std::string("hello"));

    // has_size detection
    int x = 10;
    std::cout << "int has size: " << has_size_v<int> << " => " << get_size_if_possible(x) << "\n";
    std::cout << "string has size: " << has_size_v<std::string> << " => "
              << get_size_if_possible(std::string("abc")) << "\n";

    // Compile-time factorial
    constexpr auto f6 = factorial(6);
    static_assert(f6 == 720);
    std::cout << "factorial(6) = " << f6 << "\n";
    std::cout << "factorial(10) = " << factorial(10) << "\n";

    return 0;
}
