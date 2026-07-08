// WeakPtr 核心骨架演示:Flag/WeakReference/WeakPtr 三层 + get/invalidate 行为
// 来源:WeakPtr 实战(二):核心骨架与控制块 (02-2)
// 编译:g++ -std=c++20 -Wall -Wextra -I. 16_weak_ptr_skeleton.cpp -o 16_weak_ptr_skeleton -pthread

#include "weak_ptr.hpp"

#include <iostream>

struct Foo {
    int x = 42;
    int get() const { return x; }
};

int main() {
    using namespace tamcpp::chrome;
    Foo foo{7};

    // 一个 factory 挂在 foo 上(教学:这里直接用 factory,真实场景见 17_weak_ptr_factory)
    WeakPtrFactory<Foo> fac(&foo);

    std::cout << "=== WeakPtr 基本行为 ===\n";
    auto wp = fac.get_weak_ptr();
    std::cout << "  wp 判活=" << (wp ? "yes" : "no") << ", get()=" << wp.get()
              << ", wp->x=" << wp->x << "\n";

    std::cout << "\n=== invalidate 后 ===\n";
    fac.invalidate_weak_ptrs();
    std::cout << "  wp 判活=" << (wp ? "yes" : "no") << ", get()=" << wp.get() << "\n";
    std::cout << "  was_invalidated=" << wp.was_invalidated() << " (区分被作废 vs 主动 reset)\n";
    // 注意:wp->x 此刻会 assert/debug abort —— 对应 Chromium 的 CHECK
    std::cout << "  (operator* / operator-> 在失效时 assert/CHECK,这里不再调用)\n";

    std::cout << "\n=== sizeof WeakPtr ===\n";
    std::cout << "  sizeof(WeakPtr<Foo>) = " << sizeof(WeakPtr<Foo>)
              << " (WeakReference + T*,两个指针)\n";
    std::cout << "  WeakPtrFactory 铸的所有 WeakPtr 共享同一枚 Flag → invalidate 一次集体失效\n";
    return 0;
}
