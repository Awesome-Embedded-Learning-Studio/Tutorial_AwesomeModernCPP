// 配套文章:vol4/vol2-modern-cpp17/08-ranges-pipeline-in-practice.md(管道基础)
// 演示:管道写法 vs 嵌套函数调用写法,两者等价;管道左结合。
// 编译:g++ -std=c++20 -Wall -Wextra ranges_pipe_basics.cpp -o ranges_pipe_basics
#include <iostream>
#include <ranges>
#include <vector>

int main() {
    std::vector<int> data{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    auto is_even = [](int x) { return x % 2 == 0; };
    auto times10 = [](int x) { return x * 10; };

    // 管道写法:从上往下读,像句子
    auto pipe = data | std::views::filter(is_even) | std::views::transform(times10);

    // 等价的嵌套函数调用写法:从里往外读,层级一多就难看
    auto nested = std::views::transform(std::views::filter(data, is_even), times10);

    std::cout << "pipe:  ";
    for (int x : pipe)
        std::cout << x << ' ';
    std::cout << "\nnested:";
    for (int x : nested)
        std::cout << x << ' ';
    std::cout << '\n';
}
