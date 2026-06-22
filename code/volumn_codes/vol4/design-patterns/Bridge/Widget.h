#pragma once

// pImpl(Pointer to IMPLementation)—— 桥接模式在「对外接口 x 内部实现」这一对维度上的特例。
//
// 对应文章第二~四部分。本头文件只留前向声明和一个 std::unique_ptr<Impl>,
// 所有实现细节(含 <vector>、<string> 等重型头)挪进 Widget.cpp。
//
// 关键约束:Widget 的析构、移动构造、移动赋值的定义必须挪到 cpp —— 因为
// std::unique_ptr<IncompleteType> 的析构需要完整类型,写在头里一定编译失败。

#include <cstddef>
#include <memory>

class Widget {
  public:
    Widget();
    ~Widget(); // 类外定义(Impl 完整)

    Widget(const Widget&);            // clone 深拷贝
    Widget& operator=(const Widget&); // copy-and-swap

    Widget(Widget&&) noexcept; // move 也必须类外定义 + noexcept
    Widget& operator=(Widget&&) noexcept;

    void do_work();

    // 供 main 观察 Impl 内部状态(走桥接,转发给 Impl)
    std::size_t size() const;
    int at(std::size_t i) const;
    void push_back(int value);

    friend void swap(Widget& a, Widget& b) noexcept;

  private:
    struct Impl; // 前向声明,Impl 的定义挪到 cpp
    std::unique_ptr<Impl> pImpl;
};

// 友元 swap:只是把两边的 unique_ptr 换一下,noexcept
inline void swap(Widget& a, Widget& b) noexcept {
    using std::swap;
    swap(a.pImpl, b.pImpl);
}
