// Standard: C++20
// std::accumulate 最坑的地方：返回类型 = 初始值类型。
// 初始值 0（int）会把 double 元素截断成 int 累加；0.0（double）才保留小数。
// 数学上 1.5+2.5+3.5+4.5 = 12.0，但 accumulate(v, 0) 给你 10。
#include <iostream>
#include <numeric>
#include <vector>

int main() {
    std::vector<double> v{1.5, 2.5, 3.5, 4.5}; // 数学和 = 12.0
    std::cout << "accumulate(v, 0):    " << std::accumulate(v.begin(), v.end(), 0) << '\n';
    std::cout << "accumulate(v, 0.0):  " << std::accumulate(v.begin(), v.end(), 0.0) << '\n';
    return 0;
}
