// 运行时多态示例：使用虚函数

#include <iostream>
#include <memory>

// 模拟硬件 ADC 读取
int read_adc_hw() {
    return 42;  // 模拟值
}

// 传感器接口（基类）
struct ISensor {
    virtual ~ISensor() = default;
    virtual int read() = 0;
};

// ADC 传感器实现
struct ADCSensor : ISensor {
    int read() override {
        // 直接访问 ADC 寄存器
        return read_adc_hw();
    }
};

// 温度传感器实现
struct TempSensor : ISensor {
    int read() override {
        // 模拟温度读取
        return 25;
    }
};

// 传感器轮询函数
void poll(ISensor* s) {
    int v = s->read();  // 虚函数调用 - 运行时决定
    std::cout << "传感器值: " << v << std::endl;
    // ...处理 v
}

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== 运行时多态示例 ===" << std::endl;

    ADCSensor adc;
    TempSensor temp;

    std::cout << "\n--- 通过基类指针调用 ---" << std::endl;
    poll(&adc);
    poll(&temp);

    // 使用容器存储不同传感器
    std::unique_ptr<ISensor> sensors[] = {
        std::make_unique<ADCSensor>(),
        std::make_unique<TempSensor>()
    };

    std::cout << "\n--- 使用容器遍历 ---" << std::endl;
    for (auto& s : sensors) {
        poll(s.get());
    }

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. 虚函数调用需要通过 vtable 间接查找" << std::endl;
    std::cout << "2. 每个对象携带 vptr（占用 RAM）" << std::endl;
    std::cout << "3. 可以在容器中存储不同类型的传感器" << std::endl;
    std::cout << "4. 适合需要运行时动态替换的场景" << std::endl;

    return 0;
}
