// 配套文章:vol4/vol2-modern-cpp17/08-ranges-pipeline-in-practice.md(ADC 实战)
// 演示:ADC 原始样本流的多级管道处理(过滤有效 -> 转电压 -> 校准);
//      用 iterator-pair 把惰性 view 物化成 vector(C++20 通用做法)。
// 编译:g++ -std=c++20 -Wall -Wextra ranges_pipe_adc.cpp -o ranges_pipe_adc
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <ranges>
#include <vector>

struct AdcSample {
    std::uint16_t raw;
};

// 模拟一帧 ADC 采样:几个越界噪声(50、20、5),其余有效
std::vector<AdcSample> fetch_samples() {
    return {{50}, {1024}, {3000}, {4090}, {20}, {2048}, {3500}, {5}};
}

int main() {
    auto samples = fetch_samples();

    auto pipeline = samples | std::views::filter([](const AdcSample& s) {
                        return s.raw >= 64 && s.raw <= 4000; // 丢掉越界噪声
                    }) |
                    std::views::transform([](const AdcSample& s) {
                        return s.raw * 3.3f / 4095.0f; // 原始值 -> 电压
                    }) |
                    std::views::transform([](float v) {
                        return 1.001f * v + 0.0002f * v * v; // 二阶校准曲线
                    });

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "校准后电压:";
    for (float v : pipeline)
        std::cout << ' ' << v;

    // 惰性 view 不能长期持有(源销毁即失效),需要复用就物化进容器
    std::vector<float> kept(pipeline.begin(), pipeline.end());
    std::cout << "\n物化进 vector 的样本数:" << kept.size() << '\n';
}
