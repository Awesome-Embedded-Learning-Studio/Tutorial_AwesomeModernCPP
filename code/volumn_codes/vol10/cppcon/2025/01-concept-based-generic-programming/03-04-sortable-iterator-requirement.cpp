/*
 * 验证：std::sortable 的迭代器要求
 *
 * 背景：原文初稿声称 std::sortable 要求 random_access_iterator，
 *       实际上根据 cppreference，std::sortable 的定义链为
 *       sortable -> permutable -> forward_iterator，
 *       只要求前向迭代器。
 *
 * 预期结果：所有 static_assert 通过编译，证明 std::sortable
 *           对 forward_list（前向迭代器）、list（双向迭代器）、
 *           vector（随机访问迭代器）都满足。
 *
 * 编译命令：
 *   g++ -std=c++20 -o /tmp/03-04-sortable-iterator-requirement 03-04-sortable-iterator-requirement.cpp
 *
 * 运行：
 *   /tmp/03-04-sortable-iterator-requirement
 *
 * 参考资料：
 *   - https://en.cppreference.com/w/cpp/iterator/sortable
 *
 * 编译器：GCC 16.1.1
 */

#include <iterator>
#include <concepts>
#include <forward_list>
#include <list>
#include <vector>
#include <iostream>

int main() {
    // forward_list 的迭代器是 forward_iterator，不是 random_access_iterator
    static_assert(std::forward_iterator<std::forward_list<int>::iterator>);
    static_assert(!std::random_access_iterator<std::forward_list<int>::iterator>);

    // 关键断言：forward_list 的迭代器满足 std::sortable
    static_assert(std::sortable<std::forward_list<int>::iterator>);
    static_assert(std::sortable<std::list<int>::iterator>);
    static_assert(std::sortable<std::vector<int>::iterator>);

    // permutable 的层级关系
    static_assert(std::permutable<std::forward_list<int>::iterator>);
    static_assert(std::permutable<std::list<int>::iterator>);
    static_assert(std::permutable<std::vector<int>::iterator>);

    std::cout << "All static_asserts passed!\n";
    return 0;
}
