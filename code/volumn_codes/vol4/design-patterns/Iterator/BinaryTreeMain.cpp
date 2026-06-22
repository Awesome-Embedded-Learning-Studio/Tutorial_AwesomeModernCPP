#include "BinaryTree.h"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <ranges>
#include <vector>

// 建一棵文章里反复用到的那棵树(根 4,左子树 2/1/3,右子树 6/5/7):
// 中序遍历应为:1 2 3 4 5 6 7
static BinaryTree<int> build_demo_tree(std::vector<std::unique_ptr<TreeNode<int>>>& owner) {
    owner.clear();
    owner.push_back(std::make_unique<TreeNode<int>>(1));
    owner.push_back(std::make_unique<TreeNode<int>>(3));
    owner.push_back(std::make_unique<TreeNode<int>>(5));
    owner.push_back(std::make_unique<TreeNode<int>>(7));
    owner.push_back(std::make_unique<TreeNode<int>>(2));
    owner.push_back(std::make_unique<TreeNode<int>>(6));
    owner.push_back(std::make_unique<TreeNode<int>>(4));

    auto* n1 = owner[0].get();
    auto* n3 = owner[1].get();
    auto* n5 = owner[2].get();
    auto* n7 = owner[3].get();
    auto* n2 = owner[4].get();
    auto* n6 = owner[5].get();
    auto* n4 = owner[6].get();

    n2->left = n1;
    n2->right = n3;
    n6->left = n5;
    n6->right = n7;
    n4->left = n2;
    n4->right = n6;

    BinaryTree<int> tree;
    tree.set_root(n4);
    return tree;
}

// 1. 最基础的用法:range-for 直接认它
void demo_range_for(BinaryTree<int>& tree) {
    std::cout << "range-for (inorder): ";
    for (int v : tree) {
        std::cout << v << " "; // 1 2 3 4 5 6 7
    }
    std::cout << "\n";
}

// 2. 概念检查:补全 concept 配套后,这个迭代器在 ranges 眼里到底是不是迭代器
//    文章结论:input_iterator 成立,forward_iterator 不成立(栈迭代器是单趟的)
void demo_concept_check() {
    using It = InorderIterator<int>;
    std::cout << std::boolalpha;
    std::cout << "input_iterator:    " << std::input_iterator<It> << "\n";                   // true
    std::cout << "input_range:       " << std::ranges::input_range<BinaryTree<int>> << "\n"; // true
    std::cout << "forward_iterator:  "
              << std::forward_iterator<It> << "\n"; // false(多趟保证做不到)
    std::cout << "forward_range:     "
              << std::ranges::forward_range<BinaryTree<int>> << "\n"; // false
}

// 3. ranges 适配器:惰性管道,filter / transform
void demo_ranges_views(BinaryTree<int>& tree) {
    std::cout << "filter even:       ";
    for (int v : tree | std::views::filter([](int x) { return x % 2 == 0; })) {
        std::cout << v << " "; // 2 4 6
    }
    std::cout << "\n";

    std::cout << "transform x10:     ";
    for (int v : tree | std::views::transform([](int x) { return x * 10; })) {
        std::cout << v << " "; // 10 20 30 40 50 60 70
    }
    std::cout << "\n";

    // 组合管道:filter 偶数 再 transform 平方,全程惰性、零中间容器
    std::cout << "filter even | sq:  ";
    auto view = tree | std::views::filter([](int v) { return v % 2 == 0; }) |
                std::views::transform([](int v) { return v * v; });
    for (int v : view) {
        std::cout << v << " "; // 4 16 36
    }
    std::cout << "\n";
}

// 4. 标准库算法:迭代器真正的价值——之前要写的专用递归全部删掉
void demo_classic_algorithms(BinaryTree<int>& tree) {
    // find_if:找到第一个 > 4 的就停
    auto it = std::find_if(tree.begin(), tree.end(), [](int v) { return v > 4; });
    if (it != tree.end()) {
        std::cout << "first > 4:         " << *it << "\n"; // first > 4: 5
    }

    // count_if:一边前进一边数,单趟 input 迭代器能干的事
    auto cnt = std::count_if(tree.begin(), tree.end(), [](int v) { return v % 2 == 1; });
    std::cout << "odd count:         " << cnt << "\n"; // odd count: 4

    // copy:边走边拷,不是先物化再拷
    std::vector<int> sorted_by_inorder;
    std::copy(tree.begin(), tree.end(), std::back_inserter(sorted_by_inorder));
    std::cout << "copied to vector:  ";
    for (int v : sorted_by_inorder) {
        std::cout << v << " "; // 1 2 3 4 5 6 7
    }
    std::cout << "\n";
}

int main() {
    // owner 持有所有节点(用 unique_ptr 托管,生产代码推荐写法),保证迭代期间节点存活
    std::vector<std::unique_ptr<TreeNode<int>>> owner;
    BinaryTree<int> tree = build_demo_tree(owner);

    std::cout << "=== range-for ===\n";
    demo_range_for(tree);

    std::cout << "\n=== concept check ===\n";
    demo_concept_check();

    std::cout << "\n=== ranges views ===\n";
    demo_ranges_views(tree);

    std::cout << "\n=== classic algorithms ===\n";
    demo_classic_algorithms(tree);

    return 0;
}
