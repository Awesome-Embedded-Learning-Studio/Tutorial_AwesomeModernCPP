#include <iostream>
#include <type_traits>
#include <limits>
#include <stdexcept>

template<typename T, typename U>
constexpr bool would_narrow(U u) noexcept {
    if constexpr (std::is_same_v<T, U>) {
        return false;
    } else if constexpr (std::is_floating_point_v<U> && std::is_integral_v<T>) {
        // floating to integral, check at runtime
    } else if constexpr (std::is_floating_point_v<T> && std::is_floating_point_v<U>) {
        if constexpr (std::numeric_limits<T>::digits >= std::numeric_limits<U>::digits &&
                      std::numeric_limits<T>::max() >= std::numeric_limits<U>::max() &&
                      std::numeric_limits<T>::lowest() <= std::numeric_limits<U>::lowest()) {
            return false;
        }
    } else if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
        if constexpr (std::is_signed_v<T> == std::is_signed_v<U>) {
            if constexpr (std::numeric_limits<T>::max() >= std::numeric_limits<U>::max() &&
                          std::numeric_limits<T>::lowest() <= std::numeric_limits<U>::lowest()) {
                return false;
            }
        } else if constexpr (std::is_signed_v<T> && std::is_unsigned_v<U>) {
            if constexpr (std::numeric_limits<T>::max() >= std::numeric_limits<U>::max()) {
                return false;
            }
        }
    }
    // signed -> unsigned with negative source: always narrowing
    // round-trip check can't catch this (int(-1) -> unsigned -> int(-1) is reversible on 2's complement)
    if constexpr (std::is_unsigned_v<T> && std::is_signed_v<U>) {
        if (u < 0) return true;
    }
    T t = static_cast<T>(u);
    if (static_cast<U>(t) != u) {
        return true;
    }
    if constexpr (std::is_floating_point_v<U> && std::is_integral_v<T>) {
        if (u != static_cast<U>(static_cast<long long>(u))) {
            return true;
        }
    }
    return false;
}

template<typename T, typename U>
constexpr T narrow_convert(U u) {
    if (would_narrow<T>(u)) {
        throw std::invalid_argument("narrowing conversion detected");
    }
    return static_cast<T>(u);
}

template<typename T>
class Number {
    T value_;
public:
    template<typename U>
    constexpr Number(U u) : value_(narrow_convert<T>(u)) {}
    constexpr Number(T t) : value_(t) {}
    constexpr operator T() const noexcept { return value_; }
    constexpr T get() const noexcept { return value_; }
};

int main() {
    int a = narrow_convert<int>(42.0);
    unsigned int b = narrow_convert<unsigned int>(100);
    std::cout << "a = " << a << ", b = " << b << "\n";

    try { char c = narrow_convert<char>(300); }
    catch (const std::invalid_argument& e) { std::cout << "caught: " << e.what() << "\n"; }

    try { unsigned int d = narrow_convert<unsigned int>(-1); }
    catch (const std::invalid_argument& e) { std::cout << "caught: " << e.what() << "\n"; }

    Number<int> x = 42;
    Number<int> y = 3.0;
    std::cout << "x = " << x << ", y = " << y << "\n";

    try { Number<char> c = 300; }
    catch (const std::invalid_argument& e) { std::cout << "caught: " << e.what() << "\n"; }

    return 0;
}
