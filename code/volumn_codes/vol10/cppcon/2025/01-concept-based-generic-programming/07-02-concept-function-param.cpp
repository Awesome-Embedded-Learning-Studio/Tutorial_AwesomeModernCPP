#include <iostream>
#include <vector>
#include <list>
#include <concepts>

void advance_by_2(std::random_access_iterator auto& it) {
    it += 2;
}

void advance_by_2(std::input_iterator auto& it) {
    ++it; ++it;
}

int main() {
    std::vector<int> vec = {1, 2, 3, 4, 5};
    std::list<int> lst = {1, 2, 3, 4, 5};
    auto vit = vec.begin();
    auto lit = lst.begin();
    advance_by_2(vit);
    advance_by_2(lit);
    std::cout << *vit << "\n";
    std::cout << *lit << "\n";
    return 0;
}
