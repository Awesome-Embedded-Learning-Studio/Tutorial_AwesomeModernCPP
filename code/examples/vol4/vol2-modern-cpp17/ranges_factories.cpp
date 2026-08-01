// 配套 07-ranges-basics-and-views:常用视图工厂 filter/transform/take/drop/iota/split
// 编译: g++ -std=c++20 -Wall -Wextra ranges_factories.cpp
#include <iostream>
#include <ranges>
#include <string>
#include <vector>

void dump(const char* name, auto r) {
    std::cout << name << ": ";
    for (auto x : r)
        std::cout << x << ' ';
    std::cout << '\n';
}

int main() {
    std::vector<int> data = {120, 45, 230, 67, 340, 89, 56, 180};

    // filter:只保留落在 [50,300] 的读数
    dump("filter [50,300]", data | std::views::filter([](int v) { return v >= 50 && v <= 300; }));

    // transform:12 位 ADC 原值转电压(mV 量级整数近似)
    dump("transform->mV", std::views::transform(data, [](int adc) { return adc * 3300 / 4095; }));

    // take / drop:处理数据帧的头部/尾部
    auto seq = std::views::iota(0, 10);
    dump("take 3", seq | std::views::take(3));
    dump("drop 3", std::views::iota(0, 10) | std::views::drop(3));
    dump("drop 2 | take 4", std::views::iota(0, 10) | std::views::drop(2) | std::views::take(4));

    // iota:生成 ADC 通道编号 0..15,不占任何存储
    std::cout << "iota ADC 通道: ";
    for (int ch : std::views::iota(0, 16))
        std::cout << ch << ' ';
    std::cout << '\n';

    // split:按分隔符切字符串(协议解析常用)
    std::string raw = "sensor1=25,sensor2=30,sensor3=28";
    std::cout << "split(','): ";
    for (auto sub : raw | std::views::split(',')) {
        std::string_view sv{sub.begin(), sub.end()};
        std::cout << "[" << sv << "] ";
    }
    std::cout << '\n';
    return 0;
}
