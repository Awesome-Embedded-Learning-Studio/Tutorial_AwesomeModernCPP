// CRTP（奇异递归模板模式）示例

#include <iostream>

// 模拟硬件 ADC 读取
int read_adc_hw() {
    return 42;
}

// CRTP 基类
template<typename Derived>
struct SensorBase {
    // 基类中调用派生类的方法
    int read_and_scale() {
        // static_cast 转换为派生类类型
        int v = static_cast<Derived*>(this)->read();
        return scale(v);
    }

    // 基类提供的通用实现
    int scale(int value) {
        return value * 2;  // 示例缩放
    }

    void calibrate() {
        std::cout << "执行校准..." << std::endl;
    }
};

// ADC 传感器，继承 CRTP 基类
struct ADCSensor : SensorBase<ADCSensor> {
    int read() {
        return read_adc_hw();
    }

    // 可以覆盖基类的实现
    int scale(int value) {
        return value * 3;  // ADC 特定的缩放
    }
};

// 温度传感器，继承 CRTP 基类
struct TempSensor : SensorBase<TempSensor> {
    int read() {
        return 25;
    }

    // 使用基类的默认缩放 (value * 2)
};

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== CRTP 示例 ===" << std::endl;

    ADCSensor adc;
    TempSensor temp;

    std::cout << "\n--- ADC 传感器 ---" << std::endl;
    std::cout << "原始值: " << adc.read() << std::endl;
    std::cout << "缩放后: " << adc.read_and_scale() << std::endl;
    adc.calibrate();

    std::cout << "\n--- 温度传感器 ---" << std::endl;
    std::cout << "原始值: " << temp.read() << std::endl;
    std::cout << "缩放后: " << temp.read_and_scale() << std::endl;
    temp.calibrate();

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. CRTP 让基类可以调用派生类的方法" << std::endl;
    std::cout << "2. 编译期确定类型，无虚函数开销" << std::endl;
    std::cout << "3. 可以在基类中实现代码复用" << std::endl;
    std::cout << "4. 派生类可以选择覆盖或使用默认实现" << std::endl;

    return 0;
}
