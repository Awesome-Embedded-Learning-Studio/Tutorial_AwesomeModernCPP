#include <iostream>
#include <type_traits>
#include <limits>
#include <stdexcept>

template<typename T, typename U>
constexpr bool would_narrow(U u) noexcept {
    if constexpr (std::is_same_v<T, U>) { return false; }
    else if constexpr (std::is_floating_point_v<U> && std::is_integral_v<T>) { }
    else if constexpr (std::is_floating_point_v<T> && std::is_floating_point_v<U>) {
        if constexpr (std::numeric_limits<T>::digits >= std::numeric_limits<U>::digits &&
                      std::numeric_limits<T>::max() >= std::numeric_limits<U>::max() &&
                      std::numeric_limits<T>::lowest() <= std::numeric_limits<U>::lowest()) {
            return false;
        }
    } else if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
        if constexpr (std::is_signed_v<T> == std::is_signed_v<U>) {
            if constexpr (std::numeric_limits<T>::max() >= std::numeric_limits<U>::max() &&
                          std::numeric_limits<T>::lowest() <= std::numeric_limits<U>::lowest()) { return false; }
        } else if constexpr (std::is_signed_v<T> && std::is_unsigned_v<U>) {
            if constexpr (std::numeric_limits<T>::max() >= std::numeric_limits<U>::max()) { return false; }
        }
    }
    // signed -> unsigned with negative source: always narrowing
    if constexpr (std::is_unsigned_v<T> && std::is_signed_v<U>) {
        if (u < 0) return true;
    }
    T t = static_cast<T>(u);
    if (static_cast<U>(t) != u) { return true; }
    if constexpr (std::is_floating_point_v<U> && std::is_integral_v<T>) {
        if (u != static_cast<U>(static_cast<long long>(u))) { return true; }
    }
    return false;
}

template<typename T, typename U>
constexpr T narrow_convert(U u) {
    if (would_narrow<T>(u)) throw std::invalid_argument("narrowing conversion detected");
    return static_cast<T>(u);
}

template<typename T>
class Number {
    T value_;
public:
    template<typename U> constexpr Number(U u) : value_(narrow_convert<T>(u)) {}
    constexpr Number(T t) : value_(t) {}
    constexpr operator T() const noexcept { return value_; }
    constexpr T get() const noexcept { return value_; }
    template<typename U>
    constexpr auto operator+(const Number<U>& other) const -> Number<std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;
        return Number<R>(value_ + other.get());
    }
    template<typename U>
    constexpr auto operator-(const Number<U>& other) const -> Number<std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;
        return Number<R>(value_ - other.get());
    }
    template<typename U>
    constexpr auto operator*(const Number<U>& other) const -> Number<std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;
        return Number<R>(value_ * other.get());
    }
};

int main() {
    Number<int> a = 10;
    Number<double> b = 3.5;
    auto result = a + b;
    std::cout << "10 + 3.5 = " << result << "\n";
    std::cout << "result type is Number<double>? "
              << std::is_same_v<decltype(result), Number<double>> << "\n";

    Number<unsigned int> x = 3000000000u;
    Number<unsigned int> y = 2000000000u;
    // NOTE: unsigned arithmetic wraps before narrow_convert can check.
    // This does NOT throw — the result is 705032704 (a valid unsigned int).
    // See 01-06-overflow-not-caught.cpp for the full demonstration.
    auto wrapped = x + y;
    std::cout << "3000000000u + 2000000000u = " << wrapped
              << " (wrapped, no exception)\n";
    return 0;
}
