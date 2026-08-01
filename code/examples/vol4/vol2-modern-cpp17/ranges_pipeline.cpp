// 配套 07-ranges-basics-and-views:组合管道的惰性 + take 提前终止
// 编译: g++ -std=c++20 -Wall -Wextra ranges_pipeline.cpp
#include <iomanip>
#include <iostream>
#include <ranges>
#include <vector>

int main() {
    std::vector<int> readings = {120, 45, 230, 67, 340, 89, 56, 180};
    int tf_calls = 0;

    // 整条管道建好,什么都没跑
    auto pipeline = readings | std::views::filter([](int v) { return v >= 50 && v <= 300; }) |
                    std::views::transform([&](int v) {
                        ++tf_calls;
                        return v * 3.3f / 4095;
                    }) |
                    std::views::take(3);

    std::cout << "建好管道(没遍历) transform 调用次数=" << tf_calls << '\n';

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "前 3 个有效读数的电压:\n";
    for (float v : pipeline)
        std::cout << "  " << v << '\n';
    std::cout << "遍历完 transform 总调用次数=" << tf_calls << "(take 在取够 3 个后掐断了管道)\n";
    return 0;
}
