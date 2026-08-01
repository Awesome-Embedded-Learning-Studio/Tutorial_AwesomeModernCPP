// 配套文章:vol4/vol2-modern-cpp17/08-ranges-pipeline-in-practice.md(惰性与悬垂)
// 演示:① 管道构建时不执行,只有迭代时 lambda 才被调(惰性);
//      ② view 不拥有数据,源是临时量时视图悬垂。
// 编译:g++ -std=c++20 -Wall -Wextra ranges_pipe_lazy_and_dangling.cpp -o
// ranges_pipe_lazy_and_dangling
#include <iostream>
#include <ranges>
#include <vector>

int main() {
    // ---- 惰性:带计数 lambda 证明构建时不执行 ----
    std::vector<int> data{1, 2, 3, 4, 5};
    int filter_calls = 0, transform_calls = 0;

    auto f = [&](int x) {
        ++filter_calls;
        return x % 2 == 0;
    };
    auto t = [&](int x) {
        ++transform_calls;
        return x * 10;
    };

    std::cout << "[构建前]  filter=" << filter_calls << " transform=" << transform_calls << '\n';

    auto pipe = data | std::views::filter(f) | std::views::transform(t);

    std::cout << "[构建后,迭代前] filter=" << filter_calls << " transform=" << transform_calls
              << '\n';

    int count = 0;
    for (int x : pipe) {
        (void)x;
        ++count;
    }
    std::cout << "[迭代后]  filter=" << filter_calls << " transform=" << transform_calls
              << " 产出=" << count << " 个元素\n\n";

    // ---- view 不拥有数据:源是临时量,视图悬垂 ----
    auto dangling = std::vector<int>{1, 2, 3} | std::views::filter([](int x) { return x > 1; });
    // 此刻源 vector 已销毁,dangling 指向已释放内存;迭代它是未定义行为
    std::cout << "视图 dangling 已构造(其源 vector 是临时量,已销毁)\n";
    std::cout << "此时迭代 dangling 是未定义行为\n";
}
