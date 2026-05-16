/*
 * 验证：Number<T> 的算术溢出检测在 unsigned 类型上的局限
 *
 * 背景：文章原稿声称 Number<unsigned int>(3000000000u) + Number<unsigned int>(2000000000u)
 *       会抛出 std::invalid_argument 异常。但 unsigned 算术是 well-defined wrapping，
 *       结果在传给 narrow_convert 之前就已经回绕为合法值。
 *
 * 预期结果：不会抛异常，输出回绕后的值 705032704
 *
 * 编译命令：
 *   g++ -std=c++20 -o /tmp/overflow-not-caught 01-06-overflow-not-caught.cpp
 *
 * 运行：
 *   /tmp/overflow-not-caught
 *
 * 编译器：GCC 16.1.1
 */

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
};

int main() {
    // unsigned 溢出测试：文章声称会抛异常
    Number<unsigned int> x = 3000000000u;
    Number<unsigned int> y = 2000000000u;

    // 先看原始 unsigned 算术
    unsigned int raw_sum = 3000000000u + 2000000000u;
    std::cout << "Raw unsigned sum: " << raw_sum << "\n";
    std::cout << "Would narrow (same type)? " << would_narrow<unsigned int>(raw_sum) << "\n";

    // 实际测试：不会抛异常
    try {
        auto overflow = x + y;
        std::cout << "No exception thrown! overflow = " << overflow << "\n";
    } catch (const std::invalid_argument& e) {
        std::cout << "Exception caught: " << e.what() << "\n";
    }

    // 对比：真正的窄化转换确实能被捕获
    try {
        Number<short> s = narrow_convert<short>(40000);
        std::cout << "short = " << s << "\n";
    } catch (const std::invalid_argument& e) {
        std::cout << "Narrowing correctly caught: " << e.what() << "\n";
    }

    return 0;
}
