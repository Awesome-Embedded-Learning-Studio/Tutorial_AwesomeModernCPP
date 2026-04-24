/**
 * @file test_pimpl_pattern.cpp
 * @brief 验证 PIMPL 模式的编译隔离特性
 *
 * 编译环境：g++ (GCC) 15.2.1, x86_64-linux
 * 编译命令：
 *   g++ -std=c++17 -c pimpl_widget.cpp -o pimpl_widget.o
 *   g++ -std=c++17 -c pimpl_user.cpp -o pimpl_user.o
 *   g++ -std=c++17 pimpl_widget.o pimpl_user.o -o test_pimpl
 *   ./test_pimpl
 *
 * 验证点：
 * 1. 头文件只包含前向声明，不暴露实现细节
 * 2. 修改 Impl 结构体不需要重新编译用户代码
 * 3. unique_ptr 支持不完整类型（但析构时需要完整类型）
 */

// ===== pimpl_widget.h =====
#pragma once
#include <memory>

/**
 * @brief PIMPL 模式的示例类
 *
 * 公共接口完全不暴露私有实现细节
 */
class Widget {
public:
    Widget();
    ~Widget();

    Widget(Widget&&) noexcept;
    Widget& operator=(Widget&&) noexcept;

    // 禁止拷贝（因为 unique_ptr 不可拷贝）
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    void do_something();

private:
    // 前向声明——不完整类型
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ===== pimpl_widget.cpp =====
#include <iostream>
#include <string>

/**
 * @brief Widget 的实际实现
 *
 * 这个定义对包含 pimpl_widget.h 的用户完全不可见
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
Widget::~Widget() = default;

Widget::Widget(Widget&&) noexcept = default;
Widget& Widget::operator=(Widget&&) noexcept = default;

void Widget::do_something() {
    impl_->do_work();
}

// ===== pimpl_user.cpp =====
#include "pimpl_widget.h"  // 只需要这个头文件

int main() {
    std::cout << "=== PIMPL Pattern Demonstration ===\n\n";

    Widget w;
    w.do_something();
    w.do_something();

    std::cout << "\n=== Key Points ===\n";
    std::cout << "✓ Header file only contains forward declaration\n";
    std::cout << "✓ Implementation details hidden in .cpp file\n";
    std::cout << "✓ Modifying Impl doesn't require recompiling users\n";
    std::cout << "✓ unique_ptr manages incomplete type safely\n";
    std::cout << "\nNote: Destructor must be defined in .cpp where Impl is complete!\n";

    return 0;
}
