// 编译时多态示例：使用模板

#include <iostream>
#include <array>

// 模拟硬件 ADC 读取
int read_adc_hw() {
    return 42;  // 模拟值
}

// ADC 传感器（不需要基类）
struct ADCSensor {
    int read() {
        return read_adc_hw();
    }
};

// 温度传感器
struct TempSensor {
    int read() {
        return 25;
    }
};

// 编译期多态的轮询函数
template<typename Sensor>
void poll(Sensor& s) {
    int v = s.read();  // 非虚函数，编译期解析，可内联
    std::cout << "传感器值: " << v << std::endl;
    // ...处理 v
}

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== 编译时多态示例 ===" << std::endl;

    ADCSensor adc;
    TempSensor temp;

    std::cout << "\n--- 模板函数调用 ---" << std::endl;
    poll(adc);  // 实例化 poll<ADCSensor>
    poll(temp); // 实例化 poll<TempSensor>

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. read() 调用可以被编译器内联" << std::endl;
    std::cout << "2. 不需要 vtable 和 vptr，节省 RAM" << std::endl;
    std::cout << "3. 每个模板参数生成独立的函数实例" << std::endl;
    std::cout << "4. 适合性能敏感的嵌入式场景" << std::endl;

    return 0;
}
