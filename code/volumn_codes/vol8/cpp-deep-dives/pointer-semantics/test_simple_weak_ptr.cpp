// 文章 03 测试：SimpleWeakPtr 安全失效检测
// 编译：g++ -std=c++17 -Wall -Wextra -g -o test_simple_wp test_simple_weak_ptr.cpp

#include "simple_weak_ptr.h"
#include <iostream>
#include <memory>

struct Service {
    int id;
    explicit Service(int id) : id(id) {}
    SimpleWeakPtrFactory<Service> factory{this};

    SimpleWeakPtr<Service> get_weak_ptr() { return factory.get_weak_ptr(); }
};

void test_safe_invalidation() {
    std::cout << "=== Test: safe invalidation ===\n";

    SimpleWeakPtr<Service> weak = [] {
        auto s = std::make_unique<Service>(42);
        auto w = s->get_weak_ptr();
        std::cout << "  Before destroy: is_valid() = " << std::boolalpha << w.is_valid()
                  << " (expect true)\n";
        return w;
        // Service 析构 → factory 析构 → invalidate
        // 但 AtomicFlag 仍然活着（shared_ptr 引用计数保持）
    }();

    // Flag 对象仍然存在，可以安全判空
    std::cout << "  After destroy: is_valid() = " << std::boolalpha << weak.is_valid()
              << " (expect false)\n";
    std::cout << "  get() = " << (weak.get() ? "non-null (BAD)" : "nullptr (GOOD)") << "\n\n";
}

void test_multiple_weak_ptrs() {
    std::cout << "=== Test: multiple weak ptrs ===\n";

    auto s = std::make_unique<Service>(99);
    auto w1 = s->get_weak_ptr();
    auto w2 = s->get_weak_ptr();
    auto w3 = w1;

    std::cout << "  All valid: " << w1.is_valid() << " " << w2.is_valid() << " " << w3.is_valid()
              << " (expect true true true)\n";

    s.reset();

    std::cout << "  After destroy: " << w1.is_valid() << " " << w2.is_valid() << " "
              << w3.is_valid() << " (expect false false false)\n\n";
}

void test_flag_outlives_owner() {
    std::cout << "=== Test: flag outlives owner ===\n";

    SimpleWeakPtr<Service> weak;
    {
        auto s = std::make_unique<Service>(7);
        weak = s->get_weak_ptr();
        std::cout << "  In scope: is_valid() = " << weak.is_valid() << " (expect true)\n";
    }
    // Service 和 factory 已析构，但 AtomicFlag 仍然活着
    std::cout << "  Out of scope: is_valid() = " << weak.is_valid() << " (expect false)\n";
    std::cout << "  Flag is still alive: " << (weak.is_valid() || true) << " (safe to check)\n\n";
}

int main() {
    test_safe_invalidation();
    test_multiple_weak_ptrs();
    test_flag_outlives_owner();
    return 0;
}
