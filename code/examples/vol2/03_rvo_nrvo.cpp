// 03_rvo_nrvo.cpp
// RVO/NRVO 演示：5 种返回场景对比，观察拷贝消除效果

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

Tracker make_rvo(const std::string& name) {
    return Tracker(name + "_rvo");
}

Tracker make_nrvo(const std::string& name) {
    Tracker t(name + "_nrvo");
    return t;
}

Tracker make_bad_nrvo(const std::string& name, bool flag) {
    Tracker a(name + "_a");
    Tracker b(name + "_b");
    if (flag) {
        return a;
    }
    return b;
}

Tracker make_bad_move(const std::string& name) {
    Tracker t(name + "_badmove");
    return std::move(t);
}

int main() {
    std::cout << "=== 1. RVO（返回 prvalue）===\n";
    {
        auto a = make_rvo("A");
        std::cout << "  结果: " << a.name() << "\n";
    }
    std::cout << '\n';

    std::cout << "=== 2. NRVO（返回命名变量）===\n";
    {
        auto b = make_nrvo("B");
        std::cout << "  结果: " << b.name() << "\n";
    }
    std::cout << '\n';

    std::cout << "=== 3. NRVO 失效（不同命名对象）===\n";
    {
        auto c = make_bad_nrvo("C", true);
        std::cout << "  结果: " << c.name() << "\n";
    }
    std::cout << '\n';

    std::cout << "=== 4. 错误：std::move 阻止 NRVO ===\n";
    {
        auto d = make_bad_move("D");
        std::cout << "  结果: " << d.name() << "\n";
    }

    return 0;
}
