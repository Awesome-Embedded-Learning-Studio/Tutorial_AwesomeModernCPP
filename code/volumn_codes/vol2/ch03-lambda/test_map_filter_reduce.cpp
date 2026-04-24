// 验证 map/filter/reduce 模式的正确实现
#include <algorithm>
#include <numeric>
#include <vector>
#include <iostream>
#include <string>
#include <cstdint>

struct SensorReading {
    std::string sensor_id;
    double value;
    uint32_t timestamp;
};

void demo_map_filter_reduce() {
    std::vector<SensorReading> readings = {
        {"temp_01", 23.5, 1000},
        {"temp_01", 24.1, 2000},
        {"temp_02", 45.0, 1000},
        {"temp_01", 22.8, 3000},
        {"temp_02", 47.3, 2000},
        {"temp_01", 25.0, 4000},
        {"temp_02", 44.5, 3000},
        {"temp_03", 18.2, 1000},
    };

    // === Filter：只保留 temp_01 的读数 ===
    std::vector<SensorReading> filtered;
    std::copy_if(readings.begin(), readings.end(),
                std::back_inserter(filtered),
                [](const SensorReading& r) { return r.sensor_id == "temp_01"; });

    // === Map：提取温度值 ===
    std::vector<double> values(filtered.size());
    std::transform(filtered.begin(), filtered.end(),
                  values.begin(),
                  [](const SensorReading& r) { return r.value; });

    // === Reduce：计算平均值 ===
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    double avg = sum / static_cast<double>(values.size());

    std::cout << "temp_01 readings: ";
    for (double v : values) std::cout << v << " ";
    std::cout << "\n";
    std::cout << "Average: " << avg << "\n";
}

// 封装成可复用的函数式工具
auto functional_map = [](const auto& container, auto func) {
    using Value = std::decay_t<decltype(func(*container.begin()))>;
    std::vector<Value> result;
    result.reserve(container.size());
    std::transform(container.begin(), container.end(),
                  std::back_inserter(result), func);
    return result;
};

auto functional_filter = [](const auto& container, auto pred) {
    using Value = std::decay_t<typename std::decay_t<decltype(container)>::value_type>;
    std::vector<Value> result;
    std::copy_if(container.begin(), container.end(),
                std::back_inserter(result), pred);
    return result;
};

void demo_functional_helpers() {
    std::cout << "\n=== Using functional helpers ===\n";
    // 链式调用示例：过滤偶数 -> 翻倍
    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    auto evens = functional_filter(data, [](int x) { return x % 2 == 0; });
    auto doubled = functional_map(evens, [](int x) { return x * 2; });

    std::cout << "Evens: ";
    for (int x : evens) std::cout << x << " ";
    std::cout << "\nDoubled: ";
    for (int x : doubled) std::cout << x << " ";
    std::cout << "\n";
}

int main() {
    demo_map_filter_reduce();
    demo_functional_helpers();
    return 0;
}
