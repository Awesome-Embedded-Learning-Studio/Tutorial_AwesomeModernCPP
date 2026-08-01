// 配套 07-ranges-basics-and-views:视图引用临时容器,函数返回后视图悬空
// 默认编译运行打印说明;加 -DDANGLING 后用 ASan 复现 use-after-free:
//   g++ -std=c++20 -Wall -Wextra -DDANGLING -fsanitize=address ranges_dangling.cpp
//   ./a.out
#include <iostream>
#include <ranges>
#include <vector>

#ifdef DANGLING
// 反例:返回的视图引用 local,vector 在函数返回时析构,视图立刻悬空
auto make_bad_view() {
    std::vector<int> local = {1, 2, 3, 4, 5};
    return local | std::views::filter([](int x) { return x > 2; });
}
int main() {
    auto v = make_bad_view();
    for (int x : v)
        std::cout << x << ' '; // UB:访问已释放内存
    std::cout << '\n';
}
#else
// 正例:数据源生命周期长于视图
class SensorBuffer {
    std::vector<int> data_ = {1, 2, 3, 4, 5};

  public:
    auto valid() { // 视图引用 data_,只要 SensorBuffer 还在就安全
        return data_ | std::views::filter([](int x) { return x > 2; });
    }
};
int main() {
    std::cout << "默认模式:演示正确写法(数据源与视图同生命周期)\n";
    SensorBuffer buf;
    auto v = buf.valid();
    std::cout << "buf.valid(): ";
    for (int x : v)
        std::cout << x << ' ';
    std::cout << "\n(加 -DDANGLING 并配合 -fsanitize=address 复现悬空)\n";
}
#endif
