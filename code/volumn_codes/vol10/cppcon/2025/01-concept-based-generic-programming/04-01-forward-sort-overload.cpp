#include <concepts>
#include <ranges>
#include <forward_list>
#include <vector>
#include <algorithm>
#include <iostream>
#include <iterator>
#include <functional>

template<typename R, typename C = std::less<>>
concept forward_sortable_range =
    std::ranges::forward_range<R> &&
    requires(R& r, C comp) {
        { *std::begin(r) < *std::begin(r) } -> std::convertible_to<bool>;
    };

template<std::ranges::random_access_range R, typename C = std::less<>>
    requires std::sortable<std::ranges::iterator_t<R>, C>
void my_sort(R& r, C comp = C{}) {
    std::ranges::sort(r, comp);
    std::cout << "  [random access path]\n";
}

template<typename R, typename C = std::less<>>
    requires forward_sortable_range<R, C> && (!std::ranges::random_access_range<R>)
void my_sort(R& r, C comp = C{}) {
    std::vector<std::ranges::range_value_t<R>> tmp(std::begin(r), std::end(r));
    std::ranges::sort(tmp, comp);
    std::ranges::copy(tmp, std::begin(r));
    std::cout << "  [forward iterator path: copy-sort-writeback]\n";
}

int main() {
    std::vector<int> v = {5, 3, 1, 4, 2};
    std::cout << "sort vector: ";
    my_sort(v);
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';

    std::forward_list<int> fl = {5, 3, 1, 4, 2};
    std::cout << "sort forward_list: ";
    my_sort(fl);
    for (int x : fl) std::cout << x << ' ';
    std::cout << '\n';
    return 0;
}
