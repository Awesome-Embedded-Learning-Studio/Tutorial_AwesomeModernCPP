#pragma once

#include <memory>
#include <print>
#include <string>
#include <unordered_map>

// 文章第四步演进后的最终版本:多态 clone() + std::unique_ptr 所有权语义。
// 基类把 clone 做成纯虚,强制每个派生类自己实现「如何忠实复制自己」,
// 这样通过基类指针复制时不会发生对象切片(派生部分被切掉)。
class Widget {
  public:
    virtual ~Widget() = default;

    // 返回 std::unique_ptr<Widget>(不是裸指针):所有权跟着返回值走,调用方免 delete。
    // 注意:unique_ptr 之间不支持协变返回类型,所以基类与派生类必须写完全相同的返回类型,
    // 靠函数体里 make_unique<Button>(*this) 到 unique_ptr<Widget> 的隐式转换找回具体类型。
    virtual std::unique_ptr<Widget> clone() const = 0;

    virtual void draw() const = 0;
};

class Button : public Widget {
  public:
    std::string label;

    explicit Button(std::string l) : label(std::move(l)) {}

    std::unique_ptr<Widget> clone() const override {
        // make_unique<Button>(*this) 造出 unique_ptr<Button>,隐式转换为 unique_ptr<Widget>。
        return std::make_unique<Button>(*this);
    }

    void draw() const override { std::print("Button: {}\n", label); }
};

class TextField : public Widget {
  public:
    std::string placeholder;

    explicit TextField(std::string p) : placeholder(std::move(p)) {}

    std::unique_ptr<Widget> clone() const override { return std::make_unique<TextField>(*this); }

    void draw() const override { std::print("TextField: {}\n", placeholder); }
};

// 原型注册表(Prototype Registry):集中存放「造起来很贵、可预先调好」的原型,
// 对外按名字提供「克隆一份独立副本」的能力。注册的是模板,产出的是副本。
class WidgetRegistry {
  public:
    void register_proto(const std::string& name, std::unique_ptr<Widget> proto) {
        protos_[name] = std::move(proto);
    }

    std::unique_ptr<Widget> create(const std::string& name) const {
        auto it = protos_.find(name);
        if (it == protos_.end())
            return nullptr;         // 没注册 -> 返回空,调用方自己处理
        return it->second->clone(); // 从模板克隆一份独立副本
    }

  private:
    std::unordered_map<std::string, std::unique_ptr<Widget>> protos_;
};
