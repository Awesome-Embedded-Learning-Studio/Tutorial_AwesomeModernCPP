/*
 * 验证：SafeNumericConvert 和 checked_cast
 *
 * 背景：原稿中 SafeNumericConvert 漏判了 uint32_t -> int32_t（同 size unsigned -> signed）
 *       以及 int32_t -> uint32_t（同 size signed -> unsigned）的窄化转换。
 *       checked_cast 的比较逻辑对 unsigned -> signed 场景也有符号溢出问题。
 *
 * 预期结果：编译通过，运行输出所有 checked_cast 成功的值
 *
 * 编译命令：
 *   g++ -std=c++20 -Wall -Wextra -o /tmp/08-04-safe-numeric-convert 08-04-safe-numeric-convert.cpp
 *
 * 参考资料：
 *   - https://en.cppreference.com/w/cpp/types/is_signed
 *   - https://en.cppreference.com/w/cpp/types/common_type
 *
 * 编译器：GCC 16.1.1
 */

#include <iostream>
#include <cstdint>
#include <concepts>
#include <type_traits>
#include <limits>
#include <stdexcept>

// 安全条件：To 严格更宽，或者同宽且符号性相同
template<typename From, typename To>
concept SafeNumericConvert =
    std::integral<From> && std::integral<To> &&
    (sizeof(From) < sizeof(To) ||
     (sizeof(From) == sizeof(To) &&
      std::is_signed_v<From> == std::is_signed_v<To>));

template<typename To, typename From>
    requires SafeNumericConvert<From, To>
constexpr To safe_cast(From val) {
    return static_cast<To>(val);
}

template<typename To, typename From>
    requires (std::integral<From> && std::integral<To> && !SafeNumericConvert<From, To>)
To checked_cast(From val) {
    if constexpr (std::is_signed_v<From> && std::is_unsigned_v<To>) {
        if (val < 0) throw std::overflow_error("negative to unsigned");
    } else if constexpr (std::is_unsigned_v<From> && std::is_signed_v<To>) {
        if (val > static_cast<From>(std::numeric_limits<To>::max())) {
            throw std::overflow_error("narrowing conversion would overflow");
        }
    } else {
        using Common = std::common_type_t<From, To>;
        if (static_cast<Common>(val) < static_cast<Common>(std::numeric_limits<To>::min()) ||
            static_cast<Common>(val) > static_cast<Common>(std::numeric_limits<To>::max())) {
            throw std::overflow_error("narrowing conversion would overflow");
        }
    }
    return static_cast<To>(val);
}

int main() {
    // Compile-time: safe conversions
    static_assert(SafeNumericConvert<int, long long>);
    static_assert(SafeNumericConvert<uint32_t, uint64_t>);
    static_assert(SafeNumericConvert<uint32_t, int64_t>);

    // Compile-time: unsafe conversions
    static_assert(!SafeNumericConvert<int, char>);
    static_assert(!SafeNumericConvert<uint32_t, int32_t>);
    static_assert(!SafeNumericConvert<int32_t, uint32_t>);

    // Runtime
    int x = 42;
    auto y = safe_cast<long long>(x);
    std::cout << "safe_cast<long long>(42) = " << y << "\n";

    auto w = checked_cast<char>(x);
    std::cout << "checked_cast<char>(42) = " << (int)w << "\n";

    auto u = checked_cast<int32_t>(uint32_t(100));
    std::cout << "checked_cast<int32_t>(uint32_t(100)) = " << u << "\n";

    auto v = checked_cast<unsigned>(42);
    std::cout << "checked_cast<unsigned>(42) = " << v << "\n";

    return 0;
}
