// push_back_emplace.cpp -- push_back(拷贝/移动) vs emplace_back(原位构造) 对比
// Standard: C++17
// 对应文档：vol2-modern-features/ch00-move-semantics/05-move-in-practice.md
//
// 核心结论：
//   - push_back(lvalue) 触发拷贝构造
//   - push_back(std::move(rvalue)) 触发移动构造
//   - emplace_back(构造参数) 连移动都省了，直接原位构造
#include <iostream>
#include <string>
#include <vector>

class Heavy {
    std::string name_;
    std::vector<int> data_;

  public:
    explicit Heavy(std::string name, std::size_t n) : name_(std::move(name)), data_(n, 42) {
        std::cout << "  [" << name_ << "] 构造，数据量: " << data_.size() << "\n";
    }

    Heavy(const Heavy& other) : name_(other.name_ + "_copy"), data_(other.data_) {
        std::cout << "  [" << name_ << "] 拷贝构造\n";
    }

    Heavy(Heavy&& other) noexcept : name_(std::move(other.name_)), data_(std::move(other.data_)) {
        other.name_ = "(moved-from)";
        std::cout << "  [" << name_ << "] 移动构造\n";
    }

    ~Heavy() { std::cout << "  [" << name_ << "] 析构，数据量: " << data_.size() << "\n"; }

    const std::string& name() const { return name_; }
    std::size_t data_size() const { return data_.size(); }
};

int main() {
    std::vector<Heavy> items;
    items.reserve(4);

    std::cout << "=== push_back 左值（拷贝）===\n";
    Heavy h1("Alpha", 10000);
    items.push_back(h1);

    std::cout << "\n=== push_back 右值（移动）===\n";
    Heavy h2("Beta", 10000);
    items.push_back(std::move(h2));

    std::cout << "\n=== emplace_back 原位构造 ===\n";
    items.emplace_back("Gamma", 10000);

    std::cout << "\n=== 程序结束 ===\n";
    return 0;
}
