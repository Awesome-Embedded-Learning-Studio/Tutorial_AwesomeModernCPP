// 10_enum_class.cpp
// enum class 强类型枚举：对比 C 风格 enum 问题与类型安全改进

#include <cstdint>
#include <iostream>
#include <string_view>
#include <type_traits>

enum OldColor { OldRed, OldGreen, OldBlue };
enum OldFruit { OldApple, OldOrange, OldBanana };

enum class Color : uint8_t { Red, Green, Blue };

enum class DeviceState : uint8_t { kIdle, kInitializing, kRunning, kSuspending, kError };

std::string_view to_string(DeviceState state) {
    switch (state) {
        case DeviceState::kIdle:
            return "Idle";
        case DeviceState::kInitializing:
            return "Initializing";
        case DeviceState::kRunning:
            return "Running";
        case DeviceState::kSuspending:
            return "Suspending";
        case DeviceState::kError:
            return "Error";
    }
    return "Unknown";
}

int main() {
    std::cout << "=== C 风格 enum 的问题 ===\n";
    std::cout << "  OldRed == OldApple: " << (OldRed == OldApple ? "true" : "false");
    std::cout << " (都是0，语义混淆)\n\n";

    std::cout << "=== enum class 类型安全 ===\n";
    Color c = Color::Red;
    int x = static_cast<int>(c);
    std::cout << "  Color::Red = " << x << " (需显式转换)\n";
    std::cout << "  sizeof(Color) = " << sizeof(Color) << " (uint8_t 底层)\n\n";

    std::cout << "=== 状态机 switch ===\n";
    DeviceState state = DeviceState::kIdle;
    std::cout << "  初始: " << to_string(state) << "\n";
    state = DeviceState::kInitializing;
    std::cout << "  -> " << to_string(state) << "\n";
    state = DeviceState::kRunning;
    std::cout << "  -> " << to_string(state) << "\n";

    static_assert(sizeof(Color) == 1);
    static_assert(std::is_enum_v<Color>);

    return 0;
}
