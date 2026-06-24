// Standard: C++20
// 迭代器 category：用 C++20 concept 在编译期给各种容器的迭代器「量等级」
// 五层强弱（input→forward→bidirectional→random_access→contiguous），是/否一目了然，
// 也能解释为什么 std::sort 用得了 vector 却用不了 list。
#include <array>
#include <forward_list>
#include <iostream>
#include <iterator>
#include <list>
#include <set>
#include <string>
#include <vector>

static const char* yn(bool v) {
    return v ? "是" : "否";
}

template <class It> void print_row(const char* lbl) {
    std::cout << lbl << "  " << yn(std::input_iterator<It>) << "   "
              << yn(std::forward_iterator<It>) << "    " << yn(std::bidirectional_iterator<It>)
              << "   " << yn(std::random_access_iterator<It>) << "   "
              << yn(std::contiguous_iterator<It>) << '\n';
}

int main() {
    std::cout << "类型                    input forward bidi  rand  contig\n";
    print_row<std::vector<int>::iterator>("vector<int>::iterator  ");
    print_row<std::array<int, 4>::iterator>("array<int,4>::iterator ");
    print_row<std::string::iterator>("string::iterator       ");
    print_row<int*>("int* (裸指针)          ");
    print_row<std::list<int>::iterator>("list<int>::iterator    ");
    print_row<std::set<int>::iterator>("set<int>::iterator     ");
    print_row<std::forward_list<int>::iterator>("forward_list::iterator ");
    return 0;
}
