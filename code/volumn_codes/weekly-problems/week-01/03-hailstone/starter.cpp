#include <generator>
#include <iostream>

// 产出从 n 开始的冰雹序列;到 1 之后无限产出 1
std::generator<int> hailstone(int n) {
    // 在这里写你的实现(co_yield)
}

int main() {
    int n = 0;
    std::cin >> n;
    // 消费生成器:打印序列直到(含)第一个 1,空格分隔
}
