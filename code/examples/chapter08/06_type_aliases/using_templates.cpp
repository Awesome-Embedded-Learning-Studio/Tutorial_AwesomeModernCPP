// using_templates.cpp
// 模板别名示例

#include <vector>
#include <array>
#include <type_traits>
#include <iostream>

// using 支持模板别名，typedef 不支持
template<typename T>
using Vector = std::vector<T, std::allocator<T>>;

// 嵌入式常用类型别名
template<typename T, std::size_t N>
using FixedArray = std::array<T, N>;

// 类型萃取辅助
template<typename T>
using underlying_type_t = std::underlying_type_t<T>;

// 示例枚举
enum class Mode : uint8_t { Low = 0, High = 1 };

int main() {
    std::cout << "=== Template Aliases ===\n\n";

    // 使用 Vector 别名
    Vector<int> nums = {1, 2, 3, 4, 5};
    std::cout << "Vector: ";
    for (auto n : nums) std::cout << n << " ";
    std::cout << "\n";

    // 使用 FixedArray 别名
    FixedArray<int, 10> arr;
    arr.fill(42);
    std::cout << "FixedArray[0]: " << arr[0] << "\n";

    // 使用类型萃取
    auto mode_value = underlying_type_t<Mode>(Mode::High);
    std::cout << "Mode::High underlying value: " << static_cast<int>(mode_value) << "\n";

    // 模板别名的实际应用：类型安全的寄存器数组
    template<typename T, std::size_t N>
    using RegArray = std::array<T, N>;

    RegArray<uint32_t, 8> registers{};
    registers[0] = 0x12345678;
    std::cout << "\nRegister[0]: 0x" << std::hex << registers[0] << std::dec << "\n";

    return 0;
}
