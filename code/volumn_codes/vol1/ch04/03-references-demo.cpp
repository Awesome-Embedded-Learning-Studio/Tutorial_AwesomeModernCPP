// references.cpp
// Platform: host
// Standard: C++17

#include <iostream>
#include <string>

struct SensorData {
    float temperature;
    float humidity;
    float pressure;
};

/// @brief 通过引用交换两个变量的值
void swap_by_ref(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

/// @brief 通过 const 引用打印 SensorData（不拷贝，不修改）
void print_sensor(const SensorData& data)
{
    std::cout << "温度: " << data.temperature << "°C, "
              << "湿度: " << data.humidity << "%, "
              << "气压: " << data.pressure << " hPa"
              << std::endl;
}

/// @brief 返回成员引用，允许外部修改
class Sensor {
    SensorData data_;

public:
    Sensor(float t, float h, float p)
        : data_{t, h, p}
    {
    }

    float& temperature() { return data_.temperature; }
    const SensorData& reading() const { return data_; }
};

int main()
{
    // --- 交换变量 ---
    int x = 10, y = 20;
    std::cout << "交换前: x=" << x << ", y=" << y << std::endl;
    swap_by_ref(x, y);
    std::cout << "交换后: x=" << x << ", y=" << y << std::endl;

    // --- const 引用传递大对象 ---
    SensorData reading{25.5f, 60.0f, 1013.25f};
    std::cout << "\n传感器读数: ";
    print_sensor(reading);

    // --- 返回成员引用 ---
    Sensor s(22.0f, 55.0f, 1000.0f);
    std::cout << "\n修改前: ";
    print_sensor(s.reading());

    s.temperature() = 30.0f;
    std::cout << "修改后: ";
    print_sensor(s.reading());

    // --- const 引用绑定临时对象 ---
    const std::string& label = std::string("温度传感器 #1");
    std::cout << "\n标签: " << label << std::endl;

    return 0;
}
