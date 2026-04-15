// basic_auto.cpp - auto关键字的基础用法演示
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <cstdint>

// 嵌入式场景：模拟HAL类型
using GPIO_Port = uint32_t;
constexpr GPIO_Port GPIOA = 0;

// 模拟传感器数据结构
struct SensorData {
    uint8_t sensor_id;
    float value;
    uint32_t timestamp;
};

// 返回复杂类型的函数
std::map<uint32_t, std::string>& get_sensor_names() {
    static std::map<uint32_t, std::string> names = {
        {1, "Temperature"},
        {2, "Humidity"},
        {3, "Pressure"}
    };
    return names;
}

int main() {
    std::cout << "=== auto基础用法演示 ===\n\n";

    // 1. 基本类型推导
    std::cout << "1. 基本类型推导:\n";
    auto x = 42;           // int
    auto y = 3.14;         // double
    auto z = "hello";      // const char*
    auto flag = true;      // bool

    std::cout << "  x (int): " << x << '\n';
    std::cout << "  y (double): " << y << '\n';
    std::cout << "  z (const char*): " << z << '\n';
    std::cout << "  flag (bool): " << std::boolalpha << flag << '\n';

    // 2. 无符号整型（嵌入式常用）
    std::cout << "\n2. 无符号整型:\n";
    auto irq_mask = 0xFFu;      // unsigned int
    auto address = 0x40000000u;  // unsigned int
    std::cout << "  irq_mask: 0x" << std::hex << irq_mask << std::dec << '\n';
    std::cout << "  address: 0x" << std::hex << address << std::dec << '\n';

    // 3. 范围for循环中的auto
    std::cout << "\n3. 范围for循环:\n";
    std::vector<int> numbers = {1, 2, 3, 4, 5};

    std::cout << "  按值拷贝 (auto value):\n";
    for (auto value : numbers) {
        std::cout << "    " << value << ' ';
    }
    std::cout << '\n';

    std::cout << "  const引用 (const auto& value):\n";
    for (const auto& value : numbers) {
        std::cout << "    " << value << ' ';
    }
    std::cout << '\n';

    // 4. 复杂类型简化
    std::cout << "\n4. 复杂类型简化:\n";

    // 传统写法（注释掉，太长了）
    // std::map<uint32_t, std::string>::iterator it = get_sensor_names().begin();

    // auto写法
    auto it = get_sensor_names().begin();
    std::cout << "  第一个传感器: " << it->first << " -> " << it->second << '\n';

    // 5. 函数返回值
    std::cout << "\n5. 函数返回值推导:\n";
    auto& names = get_sensor_names();
    std::cout << "  传感器数量: " << names.size() << '\n';

    // 6. 嵌入式GPIO类型
    std::cout << "\n6. 嵌入式类型:\n";
    auto port = GPIOA;
    std::cout << "  GPIO port: " << port << '\n';

    // 7. 结构体
    std::cout << "\n7. 结构体推导:\n";
    SensorData sensor{5, 23.5f, 1234567890};
    auto s = sensor;  // 按值拷贝
    std::cout << "  Sensor ID: " << +s.sensor_id << '\n';
    std::cout << "  Value: " << s.value << '\n';
    std::cout << "  Timestamp: " << s.timestamp << '\n';

    // 8. 初始化列表（注意C++17的变化）
    std::cout << "\n8. 初始化列表:\n";
    auto init_list = {1, 2, 3, 4, 5};  // std::initializer_list<int>
    std::cout << "  initializer_list大小: " << init_list.size() << '\n';

    // 9. decltype与auto的区别预览
    std::cout << "\n9. decltype与auto的区别:\n";
    int original = 42;
    const int& cref = original;

    auto a1 = cref;      // int（丢弃const和引用）
    decltype(cref) a2 = cref;  // const int&（保留）

    std::cout << "  auto推导: " << a1 << " (类型: int)\n";
    std::cout << "  decltype推导: " << a2 << " (类型: const int&)\n";

    std::cout << "\n=== 演示结束 ===\n";
    return 0;
}
