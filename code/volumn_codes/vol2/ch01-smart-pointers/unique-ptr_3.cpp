#include <memory>
#include <vector>
#include <iostream>

struct Sensor {
    int id;
    explicit Sensor(int i) : id(i) {}
};

int main() {
    std::vector<std::unique_ptr<Sensor>> sensors;

    // push_back 需要移动，因为 unique_ptr 不可拷贝
    sensors.push_back(std::make_unique<Sensor>(1));
    sensors.push_back(std::make_unique<Sensor>(2));
    sensors.push_back(std::make_unique<Sensor>(3));

    // vector 扩容时，内部的 unique_ptr 会通过移动构造转移
    // 这也是为什么 unique_ptr 的移动操作标记为 noexcept
    for (const auto& s : sensors) {
        std::cout << "Sensor id: " << s->id << "\n";
    }

    // 从函数返回 unique_ptr 也是通过移动（或 RVO）
    auto make_sensor = [](int id) -> std::unique_ptr<Sensor> {
        return std::make_unique<Sensor>(id);
    };

    auto s = make_sensor(99);
    std::cout << "Created sensor " << s->id << "\n";
}
