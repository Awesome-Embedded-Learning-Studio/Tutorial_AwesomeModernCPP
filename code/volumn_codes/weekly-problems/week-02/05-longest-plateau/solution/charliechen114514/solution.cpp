#include <iostream>

int main() {
    int n;
    if (!(std::cin >> n))
        return 0; // 防御:读不到 n 就直接退出,别让 n 处于未初始化状态

    int best = 0; // 见过的最长平台
    int cur = 0;  // 当前平台已经连了几个
    int prev = 0; // 前一个读进来的值
    for (int i = 0; i < n; i++) {
        int x;
        std::cin >> x;
        if (i == 0 || x != prev)
            cur = 1; // 开头,或者值变了:新平台从 1 起数
        else
            cur++; // 值没变:当前平台加长
        prev = x;
        if (cur > best)
            best = cur;
    }
    std::cout << best << std::endl;
}
