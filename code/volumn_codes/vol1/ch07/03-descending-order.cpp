#include <algorithm>
#include <vector>

struct DescendingOrder {
    bool operator()(int a, int b) const { return a > b; }
};

int main()
{
    std::vector<int> data = {3, 1, 4, 1, 5, 9, 2, 6};

    // 传入函数对象，实现降序排序
    std::sort(data.begin(), data.end(), DescendingOrder());

    // data 现在是 {9, 6, 5, 4, 3, 2, 1, 1}
    return 0;
}
