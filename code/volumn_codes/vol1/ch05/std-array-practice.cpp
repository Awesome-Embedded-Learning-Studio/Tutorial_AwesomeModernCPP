#include <algorithm>
#include <array>
#include <iostream>

// 函数签名清晰——类型和大小一目了然，不需要额外传长度
void print_stats(const std::array<int, 5>& data)
{
    std::cout << "元素个数: " << data.size() << "\n";

    auto [min_it, max_it] = std::minmax_element(data.begin(), data.end());
    std::cout << "最小值: " << *min_it << "\n";
    std::cout << "最大值: " << *max_it << "\n";

    int sum = 0;
    for (int x : data) { sum += x; }
    std::cout << "平均: " << static_cast<double>(sum) / data.size() << "\n";
}

int main()
{
    std::array<int, 5> scores = {85, 92, 78, 96, 88};

    std::cout << "原始数据: ";
    for (int x : scores) { std::cout << x << " "; }
    std::cout << "\n\n";

    print_stats(scores);

    std::sort(scores.begin(), scores.end());
    std::cout << "\n排序后: ";
    for (int x : scores) { std::cout << x << " "; }

    auto it = std::find(scores.begin(), scores.end(), 88);
    if (it != scores.end()) {
        std::cout << "\n找到 88，下标: " << (it - scores.begin());
    }

    std::reverse(scores.begin(), scores.end());
    std::cout << "\n反转后: ";
    for (int x : scores) { std::cout << x << " "; }
    std::cout << "\n";

    return 0;
}
