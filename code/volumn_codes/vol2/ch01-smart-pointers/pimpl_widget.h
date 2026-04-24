/**
 * @file pimpl_widget.h
 * @brief PIMPL 模式的公共接口头文件
 *
 * 注意：这个头文件只包含前向声明，不暴露任何实现细节
 * 修改 Impl 结构体定义不需要重新编译包含此头文件的代码
 */

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
