#include <iostream>
#include <functional>

template <typename T>
struct safe_int {
    T value;
    friend safe_int operator+(const safe_int& a, const safe_int& b) {
        return safe_int{std::plus<T>{}(a.value, b.value)};
    }
    friend safe_int operator*(const safe_int& a, const safe_int& b) {
        return safe_int{std::multiplies<T>{}(a.value, b.value)};
    }
};

int main() {
    safe_int<int> a{10}, b{20};
    auto c = a + b;
    auto d = a * b;
    std::cout << "10 + 20 = " << c.value << "\n";
    std::cout << "10 * 20 = " << d.value << "\n";
    return 0;
}
