/**
 * @file ex10_unique_ptr_sensor.cpp
 * @brief 练习：改造裸指针程序
 *
 * 将使用 new/delete 管理的 Sensor 裸指针程序
 * 改造为使用 std::unique_ptr + std::make_unique 的版本。
 */

#include <iostream>
#include <memory>
#include <string>

// 传感器类
class Sensor {
private:
    std::string name_;
    double value_;

public:
    explicit Sensor(const std::string& name)
        : name_(name), value_(0.0) {
        std::cout << "  Sensor(\"" << name_ << "\") 构造\n";
    }

    ~Sensor() {
        std::cout << "  Sensor(\"" << name_ << "\") 析构\n";
    }

    // 禁止拷贝（unique_ptr 管理的对象通常也不应拷贝）
    Sensor(const Sensor&) = delete;
    Sensor& operator=(const Sensor&) = delete;

    void set_value(double v) { value_ = v; }
    double get_value() const { return value_; }
    const std::string& name() const { return name_; }

    void print() const {
        std::cout << "  " << name_ << ": " << value_ << "\n";
    }
};

// ---- 裸指针版本（已注释，仅作对比） ----
void demo_raw_pointer() {
    std::cout << "--- 裸指针版本 (注释中) ---\n";
    // Sensor* s = new Sensor("Temperature");
    // s->set_value(36.5);
    // s->print();
    // delete s;  // 容易忘记！如果 set_value 抛异常就会泄漏
    std::cout << "  (已跳过，见注释中的代码)\n\n";
}

// ---- unique_ptr 版本 ----
void demo_unique_ptr() {
    std::cout << "--- unique_ptr 版本 ---\n";

    // 使用 make_unique 创建，无需手动 delete
    auto s = std::make_unique<Sensor>("Temperature");
    s->set_value(36.5);
    s->print();

    // 离开作用域时自动析构，即使发生异常也不会泄漏
    std::cout << "  离开作用域...\n";
}

// ---- 在容器中使用 unique_ptr ----
#include <vector>

void demo_unique_ptr_container() {
    std::cout << "\n--- unique_ptr 在容器中使用 ---\n";

    std::vector<std::unique_ptr<Sensor>> sensors;

    // 使用 std::move 转移所有权到容器中
    sensors.push_back(std::make_unique<Sensor>("Temp"));
    sensors.push_back(std::make_unique<Sensor>("Humidity"));
    sensors.push_back(std::make_unique<Sensor>("Pressure"));

    for (const auto& sensor : sensors) {
        sensor->set_value(42.0);
        sensor->print();
    }

    std::cout << "  离开作用域，容器自动销毁所有 sensor...\n";
}

int main() {
    std::cout << "===== 改造裸指针为 unique_ptr =====\n\n";

    demo_raw_pointer();
    demo_unique_ptr();
    demo_unique_ptr_container();

    std::cout << "\n要点:\n";
    std::cout << "  1. make_unique 自动管理生命周期，无需 delete\n";
    std::cout << "  2. 异常安全：即使抛异常也不会泄漏\n";
    std::cout << "  3. 所有权唯一，不能拷贝但可以 std::move\n";

    return 0;
}
