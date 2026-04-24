// 验证偏应用和柯里化概念
#include <iostream>
#include <functional>
#include <vector>
#include <algorithm>

// 用 lambda 实现偏应用
auto make_adder(int base) {
    return [base](int x) { return base + x; };
}

// 更通用的偏应用：固定前 N 个参数
auto partial = [](auto f, auto... fixed_args) {
    return [f = std::move(f), ...fixed_args = std::move(fixed_args)](auto&&... rest_args) {
        return f(fixed_args..., std::forward<decltype(rest_args)>(rest_args)...);
    };
};

int main() {
    std::cout << "=== Testing make_adder ===\n";
    auto add_10 = make_adder(10);
    auto add_20 = make_adder(20);
    std::cout << "add_10(5) = " << add_10(5) << "\n";  // 15
    std::cout << "add_20(5) = " << add_20(5) << "\n";  // 25

    std::cout << "\n=== Testing generic partial ===\n";
    auto add = [](int a, int b, int c) { return a + b + c; };

    // 固定第一个参数为 1
    auto add1 = partial(add, 1);
    std::cout << "partial(add, 1)(2, 3) = " << add1(2, 3) << "\n";   // 6

    // 固定前两个参数
    auto add1_2 = partial(add, 1, 2);
    std::cout << "partial(add, 1, 2)(3) = " << add1_2(3) << "\n";    // 6

    std::cout << "\n=== Testing practical example ===\n";
    // 更实用的例子：创建预设阈值的过滤器
    auto make_threshold_filter = [](int threshold) {
        return [threshold](const std::vector<int>& data) {
            std::vector<int> result;
            std::copy_if(data.begin(), data.end(),
                        std::back_inserter(result),
                        [threshold](int x) { return x > threshold; });
            return result;
        };
    };

    auto filter_above_50 = make_threshold_filter(50);
    auto filter_above_80 = make_threshold_filter(80);

    std::vector<int> data = {12, 45, 67, 89, 23, 90};
    auto r1 = filter_above_50(data);   // {67, 89, 90}
    auto r2 = filter_above_80(data);   // {89, 90}

    std::cout << "Above 50: ";
    for (int x : r1) std::cout << x << " ";
    std::cout << "\nAbove 80: ";
    for (int x : r2) std::cout << x << " ";
    std::cout << "\n";

    return 0;
}
