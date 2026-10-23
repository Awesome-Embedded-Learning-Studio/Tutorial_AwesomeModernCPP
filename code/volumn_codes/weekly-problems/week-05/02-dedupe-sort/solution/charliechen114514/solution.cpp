#include <iostream>
#include <set>

int main() {
    std::set<int> s;
    int x;
    while (std::cin >> x) {
        s.insert(x);
    }
    bool first = true;
    for (int v : s) {
        if (!first) std::cout << ' ';
        std::cout << v;
        first = false;
    }
    std::cout << '\n';
}
