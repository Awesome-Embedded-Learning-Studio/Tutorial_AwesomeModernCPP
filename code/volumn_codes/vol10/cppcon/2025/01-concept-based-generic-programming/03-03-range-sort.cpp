#include <ranges>
#include <algorithm>
#include <vector>
#include <string>
#include <iostream>

template<std::ranges::random_access_range R, typename Comp = std::ranges::less>
    requires std::sortable<std::ranges::iterator_t<R>, Comp>
void my_sort(R&& r, Comp comp = {}) {
    std::ranges::sort(std::forward<R>(r), comp);
}

int main() {
    std::vector<double> vd = {3.14, 1.41, 2.72, 0.58};
    my_sort(vd);
    for (double x : vd) std::cout << x << " ";
    std::cout << "\n";
    std::vector<std::string> vs = {"hello", "world", "cpp", "ranges"};
    my_sort(vs, std::ranges::greater{});
    for (const auto& s : vs) std::cout << s << " ";
    std::cout << "\n";
    return 0;
}
