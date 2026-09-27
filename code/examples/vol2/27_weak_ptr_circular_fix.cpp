#include <iostream>
#include <memory>
#include <string>

struct NodeFixed {
    std::string name;
    std::shared_ptr<NodeFixed> next;
    std::weak_ptr<NodeFixed> prev; // 改为 weak_ptr

    explicit NodeFixed(const std::string& n) : name(n) {
        std::cout << "Node(" << name << ") 构造\n";
    }
    ~NodeFixed() { std::cout << "~Node(" << name << ") 析构\n"; }
};

void fixed_circular_reference() {
    auto a = std::make_shared<NodeFixed>("A");
    auto b = std::make_shared<NodeFixed>("B");

    a->next = b; // A → B（B 的强引用计数: 1 → 2）
    b->prev = a; // B ⇢ A（弱引用，A 的强引用计数不变，仍然是 1）

    std::cout << "准备离开函数...\n";
    // 函数结束时：
    // a 离开作用域，A 的强引用计数: 1 → 0，A 被销毁
    //   A 的析构会销毁 A->next，B 的强引用计数: 2 → 1
    // b 离开作用域，B 的强引用计数: 1 → 0，B 被销毁
    // 所有节点都被正确释放！
}

int main() {
    fixed_circular_reference();
    return 0;
}
