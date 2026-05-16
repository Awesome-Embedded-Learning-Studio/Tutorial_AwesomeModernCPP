#include <iostream>
#include <concepts>
#include <type_traits>
#include <vector>
#include <list>

template<typename T>
concept SortableContainer = requires(T t) {
    requires std::ranges::range<T>;
    requires std::totally_ordered<typename T::value_type>;
};

template<typename T>
concept RandomAccessSortable = SortableContainer<T> && requires(T t) {
    requires std::random_access_iterator<typename T::iterator>;
};

void process(SortableContainer auto& c) {
    std::cout << "sortable container\n";
}

void process(RandomAccessSortable auto& c) {
    std::cout << "random access sortable container\n";
}

int main() {
    std::list<int> lst = {3, 1, 2};
    std::vector<int> vec = {3, 1, 2};
    process(lst);
    process(vec);
    return 0;
}
