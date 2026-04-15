// enum_class_memory.cpp
// 示例：使用 enum class 节省内存

#include <cstdint>
#include <iostream>

// 指定底层类型节省内存
enum class SensorState : uint8_t {
    Off = 0,
    Init = 1,
    Ready = 2,
    Error = 3
};

// 与 C 接口互操作
extern "C" void hw_set_mode(uint8_t mode);

enum class Mode : uint8_t { Low = 0, High = 1 };

// 模拟硬件函数
void hw_set_mode(uint8_t mode) {
    std::cout << "Hardware mode set to: " << static_cast<int>(mode) << "\n";
}

int main() {
    // 验证大小
    static_assert(sizeof(SensorState) == 1, "SensorState 应该是 1 字节");

    SensorState state = SensorState::Ready;
    std::cout << "State value: " << static_cast<int>(state) << "\n";
    std::cout << "State size: " << sizeof(state) << " byte\n";

    // switch 语句
    switch (state) {
        case SensorState::Off:  std::cout << "State: Off\n"; break;
        case SensorState::Init: std::cout << "State: Init\n"; break;
        case SensorState::Ready: std::cout << "State: Ready\n"; break;
        case SensorState::Error: std::cout << "State: Error\n"; break;
    }

    // 与 C 接口互操作
    hw_set_mode(static_cast<uint8_t>(Mode::High));

    return 0;
}
