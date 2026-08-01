// 配套 07-ranges-basics-and-views:嵌入式实战 温度传感器数据处理流水线
// 编译: g++ -std=c++20 -Wall -Wextra ranges_sensor_pipeline.cpp
#include <iomanip>
#include <iostream>
#include <ranges>
#include <vector>

int main() {
    // 模拟一帧温度读数,夹杂异常值(传感器掉线时 999,断路时 -200)
    std::vector<int> readings = {23, 999, 25, -200, 27, 22, 999, 26};

    // 过滤异常 -> 摄氏转华氏,全程零临时容器
    auto processed = readings | std::views::filter([](int t) { return t >= -50 && t <= 150; }) |
                     std::views::transform([](int t) { return t * 9.0 / 5.0 + 32.0; });

    double sum = 0.0;
    int count = 0;
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "有效读数(F): ";
    for (double f : processed) {
        std::cout << f << ' ';
        sum += f;
        ++count;
    }
    std::cout << '\n';
    if (count)
        std::cout << "平均温度: " << (sum / count) << " F\n";
    return 0;
}
