/**
 * @file pimpl_widget.cpp
 * @brief PIMPL 模式的实现文件
 *
 * 这里定义了 Widget::Impl 的完整实现
 * 包含此文件编译的代码才能看到 Impl 的具体细节
 */

#include "pimpl_widget.h"
#include <iostream>
#include <string>

/**
 * @brief Widget 的实际实现
 *
 * 这个定义对包含 pimpl_widget.h 的用户完全不可见
 * 你可以随意修改这个结构，而不需要重新编译用户代码
 */
struct Widget::Impl {
    std::string name;
    int count;

    Impl() : name("default"), count(0) {}

    void do_work() {
        ++count;
        std::cout << name << " working (count=" << count << ")\n";
    }
};

Widget::Widget() : impl_(std::make_unique<Impl>()) {}

// 注意：析构函数必须在 cpp 文件中定义，此时 Impl 是完整类型
// 如果在头文件中 = default，编译器会在包含头文件时尝试实例化析构
// 而此时 Impl 是不完整类型，会导致编译错误
Widget::~Widget() = default;

Widget::Widget(Widget&&) noexcept = default;
Widget& Widget::operator=(Widget&&) noexcept = default;

void Widget::do_something() {
    impl_->do_work();
}
