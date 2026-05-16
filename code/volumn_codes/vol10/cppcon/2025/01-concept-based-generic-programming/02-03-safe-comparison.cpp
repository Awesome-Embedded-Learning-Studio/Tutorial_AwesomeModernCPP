#include <iostream>
#include <type_traits>

template <typename T>
struct safe_int {
    T value;
};

template <typename T, typename U>
bool operator<(const safe_int<T>& a, const safe_int<U>& b) {
    if constexpr (std::is_signed_v<T> && std::is_unsigned_v<U>) {
        if (a.value < 0) return true;
        return static_cast<std::make_unsigned_t<T>>(a.value) < b.value;
    } else if constexpr (std::is_unsigned_v<T> && std::is_signed_v<U>) {
        if (b.value < 0) return false;
        return a.value < static_cast<std::make_unsigned_t<U>>(b.value);
    } else {
        return a.value < b.value;
    }
}

int main() {
    safe_int<int> a{-1};
    safe_int<unsigned int> b{2};
    std::cout << "(a < b) = " << (a < b) << "\n";  // Now correctly prints 1 (true)
    return 0;
}
