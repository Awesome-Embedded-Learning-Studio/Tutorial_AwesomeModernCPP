// pImpl 的实现细节全藏在这里。对外头文件 widget.h 不含 <vector>、<string>,
// Impl 的成员再怎么变,都只动本编译单元,外部世界一无所知。
#include "Widget.h"

#include <iostream>
#include <string>
#include <vector>

struct Widget::Impl {
    std::vector<int> data;
    std::string name;
    void do_work_impl() { std::cout << "doing heavy work, data.size=" << data.size() << "\n"; }
    std::unique_ptr<Impl> clone() const {
        return std::make_unique<Impl>(*this); // 深拷贝
    }
};

Widget::Widget() : pImpl(std::make_unique<Impl>()) {}

Widget::~Widget() = default; // 关键:这里只能写 = default,且必须落在 cpp

// 拷贝构造:转交给 Impl::clone(),空指针安全
Widget::Widget(const Widget& other) : pImpl(other.pImpl ? other.pImpl->clone() : nullptr) {}

// copy-and-swap:强异常安全
Widget& Widget::operator=(const Widget& other) {
    Widget tmp(other); // 先拷一份临时对象
    swap(*this, tmp);  // 再和 *this 交换
    return *this;      // tmp 析构时自动释放旧 Impl
}

// move 的 = default 同样要写在 cpp 里:它内部要搬 unique_ptr<Impl>,也得等 Impl 完整
Widget::Widget(Widget&&) noexcept = default;
Widget& Widget::operator=(Widget&&) noexcept = default;

void Widget::do_work() {
    pImpl->do_work_impl();
}

// move-from 之后的 Widget 处于「空状态」(pImpl == nullptr),访问需做空指针保护
std::size_t Widget::size() const {
    return pImpl ? pImpl->data.size() : 0;
}

int Widget::at(std::size_t i) const {
    return pImpl->data.at(i);
}

void Widget::push_back(int value) {
    pImpl->data.push_back(value);
}
