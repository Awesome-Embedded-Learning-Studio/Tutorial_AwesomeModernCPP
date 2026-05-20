// 01_rvalue_reference.cpp
// 右值引用与值类别：构造、拷贝、移动、析构全过程追踪

#include <iostream>
#include <string>
#include <utility>

class Tracker {
    std::string name_;

  public:
    explicit Tracker(std::string name) : name_(std::move(name)) {
        std::cout << "  [" << name_ << "] 构造\n";
    }
    Tracker(const Tracker& other) : name_(other.name_ + "_copy") {
        std::cout << "  [" << name_ << "] 拷贝构造\n";
    }
    Tracker(Tracker&& other) noexcept : name_(std::move(other.name_)) {
        other.name_ = "(moved-from)";
        std::cout << "  [" << name_ << "] 移动构造\n";
    }
    ~Tracker() { std::cout << "  [" << name_ << "] 析构\n"; }
    const std::string& name() const { return name_; }
};

Tracker make_tracker(std::string name) {
    return Tracker(std::move(name));
}

int main() {
    std::cout << "=== 1. 基本构造 ===\n";
    Tracker a("A");
    std::cout << '\n';

    std::cout << "=== 2. 拷贝构造 ===\n";
    Tracker b = a;
    std::cout << "  a.name = " << a.name() << "\n  b.name = " << b.name() << "\n\n";

    std::cout << "=== 3. 移动构造（显式 std::move）===\n";
    Tracker c = std::move(a);
    std::cout << "  a.name = " << a.name() << "\n  c.name = " << c.name() << "\n\n";

    std::cout << "=== 4. 返回临时对象 ===\n";
    Tracker d = make_tracker("D");
    std::cout << "  d.name = " << d.name() << "\n\n";

    std::cout << "=== 5. 程序结束，析构顺序 ===\n";
    return 0;
}
