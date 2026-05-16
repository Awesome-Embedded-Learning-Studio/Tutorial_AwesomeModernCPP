#include <iostream>
#include <concepts>
#include <type_traits>

template<typename T>
concept Number = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
    { a - b } -> std::convertible_to<T>;
    { a * b } -> std::convertible_to<T>;
    { a / b } -> std::convertible_to<T>;
    { -a }    -> std::convertible_to<T>;
};

template<Number T>
T compute(T x, T y) {
    return (x + y) * 2 - y;
}

struct FixedPoint {
    int value;
    int scale;
    FixedPoint(int v, int s = 100) : value(v), scale(s) {}
    FixedPoint operator+(const FixedPoint& o) const { return {value + o.value, scale}; }
    FixedPoint operator-(const FixedPoint& o) const { return {value - o.value, scale}; }
    FixedPoint operator*(const FixedPoint& o) const { return {value * o.value / scale, scale}; }
    FixedPoint operator/(const FixedPoint& o) const { return {value * o.scale / o.value, scale}; }
    FixedPoint operator-() const { return {-value, scale}; }
};

int main() {
    std::cout << compute(3, 5) << "\n";
    std::cout << compute(3.14, 2.72) << "\n";
    std::cout << compute(FixedPoint{300}, FixedPoint{500}).value << "\n";
    return 0;
}
