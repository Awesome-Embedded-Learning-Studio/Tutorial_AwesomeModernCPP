#include <vector>
#include <list>
#include <iostream>

template<typename Iter>
void my_advance(Iter& it, int n) {
    if constexpr (requires(Iter i, int m) { i += m; }) {
        it += n;
    } else {
        for (int i = 0; i < n; ++i) ++it;
    }
}

int main() {
    std::vector<int> vec = {10, 20, 30, 40, 50};
    auto vit = vec.begin();
    my_advance(vit, 2);
    std::cout << *vit << "\n";
    std::list<int> lst = {10, 20, 30, 40, 50};
    auto lit = lst.begin();
    my_advance(lit, 2);
    std::cout << *lit << "\n";
    return 0;
}
