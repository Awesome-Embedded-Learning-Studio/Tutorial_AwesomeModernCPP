// 11_structured_bindings.cpp
// 结构化绑定：pair、tuple、数组、结构体与自定义类型解包

#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <utility>

struct SensorReading {
    uint8_t sensor_id;
    float value;
    uint32_t timestamp;
    bool is_valid;
};

int main() {
    std::cout << "=== 结构化绑定演示 ===\n\n";

    std::cout << "1. pair 解包 (map insert):\n";
    std::map<int, std::string> m = {{1, "one"}, {2, "two"}};
    auto [it, inserted] = m.insert({3, "three"});
    std::cout << "  inserted=" << inserted << ", key=" << it->first << ", value=" << it->second
              << "\n\n";

    std::cout << "2. map 遍历:\n";
    for (const auto& [id, name] : m) {
        std::cout << "  [" << id << "] = " << name << "\n";
    }
    std::cout << "\n";

    std::cout << "3. tuple 解包:\n";
    auto [x, y, z] = std::make_tuple(10, 3.14, "hello");
    std::cout << "  x=" << x << ", y=" << y << ", z=" << z << "\n\n";

    std::cout << "4. 数组解包:\n";
    int rgb[3] = {255, 128, 0};
    auto [r, g, b] = rgb;
    std::cout << "  r=" << r << ", g=" << g << ", b=" << b << "\n\n";

    std::cout << "5. 结构体解包:\n";
    SensorReading reading{5, 23.5f, 1234567890, true};
    auto [id, val, ts, valid] = reading;
    std::cout << "  id=" << +id << ", value=" << val << ", valid=" << valid << "\n";

    return 0;
}
