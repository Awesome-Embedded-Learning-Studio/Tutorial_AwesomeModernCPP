// 配套 07-ranges-basics-and-views:视图惰性 + O(1) 拷贝 + 引用语义
// 编译: g++ -std=c++20 -Wall -Wextra ranges_laziness.cpp
#include <iostream>
#include <ranges>
#include <vector>

int main() {
    std::vector<int> data = {1, 2, 3, 4, 5};
    int pred_calls = 0;

    // 建视图:谓词一次都不调用
    auto v = data | std::views::filter([&](int x) {
                 ++pred_calls;
                 return x > 2;
             });
    std::cout << "建视图后(还没遍历)谓词调用次数=" << pred_calls << "\n";

    // 遍历时才执行;filter 找首个匹配要扫过 1,2,3(3次)
    for (int x : v) {
        std::cout << "取到 " << x << ", 此刻谓词已调用 " << pred_calls << " 次\n";
    }

    // 视图是引用语义:改源,重新遍历看到新值
    data[2] = 300;
    std::cout << "改 data[2]=300 后重新遍历: ";
    for (int x : v)
        std::cout << x << ' ';
    std::cout << "\n";

    // 拷贝一个视图 = 拷贝几个迭代器/谓词,不复制任何元素
    auto v2 = v; // O(1):底层 vector 不参与拷贝
    std::cout << "拷贝视图后 v2 首元素=" << *v2.begin() << "\n";
    return 0;
}
