#pragma once

#include <iterator>
#include <memory>
#include <stack>

// 文章配套代码:迭代器模式(Iterator Pattern)
//
// 演进路径(见 documents/vol4-advanced/vol4-generics-patterns/18-iterator.md):
//   1. 最原始:把遍历写进调用方(递归 + 目标容器)——反面教材
//   2. 外部迭代器(栈):operator*/++/== + begin/end ——能过 range-for,但进不了 ranges
//   3. 补全 concept 配套:后缀 ++ + iterator_concept + 关联类型——本文件就是这个最终版本
//   4. 协程生成器:见文章末尾,本工程不展开
//
// 本头文件实现的是「文章里最终/最完整」的那一版栈式中序迭代器:
//   - 同时提供前缀与后缀 operator++(后缀是关键,缺了它 input_iterator 概念不成立)
//   - 带 iterator_concept / iterator_category / value_type / difference_type / pointer / reference
//   - 因此能直接喂给 std::ranges 的 views::filter / views::transform,以及 ranges 算法

template <typename T> struct TreeNode {
    T val;
    TreeNode* left = nullptr;
    TreeNode* right = nullptr;
    explicit TreeNode(T v) : val(v) {}
};

// 栈式中序迭代器:用一个栈记住「还没访问完的祖先链」
// 注意:它的天花板是 std::input_iterator(单趟、不可多趟),升不到 forward_iterator
template <typename T> class InorderIterator {
  public:
    using Node = TreeNode<T>;

    // C++20 迭代器标配:用 iterator_concept 声明种类(不是老标准的 iterator_category)
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag; // 兼容老算法
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

    InorderIterator() = default; // 默认构造出来的就是 end()(栈为空)
    explicit InorderIterator(Node* root) {
        push_left(root); // 一路把左子树压栈,停在最左叶
    }

    reference operator*() const { return stack_.top()->val; }

    InorderIterator& operator++() { // 前缀 ++
        Node* node = stack_.top();
        stack_.pop(); // 访问完当前节点,弹出
        if (node->right)
            push_left(node->right); // 转向右子树,继续压左链
        return *this;
    }

    void operator++(int) { ++(*this); } // 后缀 ++(关键!转发给前缀,丢掉返回值)

    bool operator==(const InorderIterator& other) const {
        // 两个都空(都到 end)就算相等;否则比较栈顶指针
        if (stack_.empty() && other.stack_.empty())
            return true;
        if (stack_.empty() != other.stack_.empty())
            return false;
        return stack_.top() == other.stack_.top();
    }

  private:
    std::stack<Node*> stack_;

    void push_left(Node* node) {
        while (node) {
            stack_.push(node);
            node = node->left;
        }
    }
};

// 给树套一层薄壳,提供 begin()/end(),让 range-for 和 ranges 适配器能直接认它
template <typename T> class BinaryTree {
  public:
    using Node = TreeNode<T>;
    using iterator = InorderIterator<T>;

    void set_root(Node* r) { root_ = r; }
    iterator begin() { return iterator(root_); }
    iterator end() { return iterator(); } // 默认构造 = end()

  private:
    Node* root_ = nullptr;
};
