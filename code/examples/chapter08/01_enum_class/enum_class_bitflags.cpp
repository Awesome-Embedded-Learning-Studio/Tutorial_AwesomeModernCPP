// enum_class_bitflags.cpp
// 示例：为 enum class 实现位运算符支持

#include <cstdint>
#include <type_traits>
#include <iostream>

// 辅助函数：将枚举转换为其底层类型
template<typename E>
constexpr auto to_ut(E e) noexcept {
    return static_cast<std::underlying_type_t<E>>(e);
}

// 定义位标志枚举
enum class Flags : uint8_t {
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
    Exec = 1 << 2
};

// 位运算符重载
inline Flags operator|(Flags a, Flags b) {
    return static_cast<Flags>(to_ut(a) | to_ut(b));
}

inline Flags& operator|=(Flags& a, Flags b) {
    a = a | b;
    return a;
}

inline Flags operator&(Flags a, Flags b) {
    return static_cast<Flags>(to_ut(a) & to_ut(b));
}

inline Flags operator~(Flags a) {
    return static_cast<Flags>(~to_ut(a));
}

inline bool any(Flags f) { return to_ut(f) != 0; }

int main() {
    // 组合权限
    Flags perms = Flags::Read | Flags::Write;
    std::cout << "Permissions value: " << to_ut(perms) << "\n";

    // 检查权限
    if (any(perms & Flags::Write)) {
        std::cout << "Has write permission\n";
    }

    // 添加执行权限
    perms |= Flags::Exec;
    std::cout << "Updated permissions: " << to_ut(perms) << "\n";

    // 移除写权限
    perms = perms & ~Flags::Write;
    std::cout << "After removing Write: " << to_ut(perms) << "\n";

    return 0;
}
