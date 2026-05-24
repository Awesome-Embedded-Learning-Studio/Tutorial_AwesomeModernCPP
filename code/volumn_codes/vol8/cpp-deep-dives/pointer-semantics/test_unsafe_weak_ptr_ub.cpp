// 文章 02 测试：UnsafeWeakPtr UB 演示
// 编译（观察 UB）：
//   g++ -std=c++17 -O0 -g -o test_unsafe_ub test_unsafe_weak_ptr_ub.cpp
// 编译（ASan 检测 UB）：
//   g++ -std=c++17 -O0 -g -fsanitize=address,undefined -o test_unsafe_ub_asan
//   test_unsafe_weak_ptr_ub.cpp

#include "unsafe_weak_ptr.h"
#include <iostream>
#include <memory>

struct Widget {
    int value = 42;
    UnsafeWeakPtrFactory<Widget> factory{this};

    UnsafeWeakPtr<Widget> get_weak_ptr() { return factory.get_weak_ptr(); }
};

void test_ub_after_owner_destroyed() {
    std::cout << "=== Test: UB after owner destroyed ===\n";

    UnsafeWeakPtr<Widget> weak = [] {
        auto w = std::make_unique<Widget>();
        return w->get_weak_ptr();
        // w 在这里析构 → factory 析构 → Flag 析构
    }();

    // ⚠️ UB：flag_ 指向已销毁的 Flag
    std::cout << "  is_valid() = " << std::boolalpha << weak.is_valid() << '\n';

    if (auto* p = weak.get()) {
        std::cout << "  value = " << p->value << " (UB: reading freed memory)\n";
    } else {
        std::cout << "  get() returned nullptr (result is UB-dependent)\n";
    }
    std::cout << "  Note: run with -fsanitize=address to see heap-use-after-free\n\n";
}

void test_valid_while_owner_alive() {
    std::cout << "=== Test: valid while owner alive ===\n";

    auto w = std::make_unique<Widget>();
    auto weak = w->get_weak_ptr();

    std::cout << "  is_valid() = " << std::boolalpha << weak.is_valid() << " (expect true)\n";
    std::cout << "  get()->value = " << weak.get()->value << " (expect 42)\n";

    w.reset();
    // Flag 随 Widget 析构，weak.flag_ 悬垂
    std::cout << "  after reset: is_valid() = " << weak.is_valid()
              << " (UB, may print anything)\n\n";
}

int main() {
    test_valid_while_owner_alive();
    test_ub_after_owner_destroyed();
    return 0;
}
