// 生命周期验证测试：Chrome-like WeakPtr vs UnsafeWeakPtr
// 编译：g++ -std=c++17 -O0 -g -o test_lifetime test_lifetime_verification.cpp

#include "weak_ptr_factory.h"
#include <iostream>
#include <memory>

// ===== Chrome-like WeakPtr 测试 =====

class Session {
  public:
    explicit Session(int id) : id_(id) { std::cout << "Session(" << id_ << ") constructed\n"; }

    ~Session() { std::cout << "Session(" << id_ << ") destroyed\n"; }

    WeakPtr<Session> get_weak_ptr() { return factory_.get_weak_ptr(); }

    void do_work() const { std::cout << "Session(" << id_ << ") doing work\n"; }

    int id() const { return id_; }

  private:
    int id_;
    // Factory 放在最后——确保在其他成员析构之前 invalidate
    WeakPtrFactory<Session> factory_{this};
};

void test_weak_ptr_survives_owner() {
    std::cout << "=== Test: WeakPtr survives Owner ===\n";

    WeakPtr<Session> weak = [] {
        auto s = std::make_unique<Session>(42);
        auto w = s->get_weak_ptr();
        std::cout << "  Before destroy: valid = " << std::boolalpha << w.is_valid() << "\n";
        return w;
        // Session 在这里析构
    }();

    std::cout << "  After destroy: valid = " << std::boolalpha << weak.is_valid() << "\n";
    std::cout << "  get() returns: " << (weak.get() ? "non-null (BAD)" : "nullptr (GOOD)")
              << "\n\n";
}

void test_multiple_weak_ptrs() {
    std::cout << "=== Test: Multiple WeakPtrs ===\n";

    auto s = std::make_unique<Session>(99);
    auto w1 = s->get_weak_ptr();
    auto w2 = s->get_weak_ptr();
    auto w3 = w1;

    std::cout << "  All valid: " << w1.is_valid() << " " << w2.is_valid() << " " << w3.is_valid()
              << "\n";

    s.reset();

    std::cout << "  After destroy: " << w1.is_valid() << " " << w2.is_valid() << " "
              << w3.is_valid() << "\n\n";
}

void test_weak_ptr_in_callback() {
    std::cout << "=== Test: WeakPtr in simulated callback ===\n";

    // 模拟异步回调场景
    WeakPtr<Session> weak;

    {
        auto s = std::make_unique<Session>(7);
        weak = s->get_weak_ptr();

        // 模拟回调执行时对象还活着
        if (auto* self = weak.get()) {
            self->do_work(); // 安全
        }
    }

    // 模拟回调执行时对象已销毁
    if (auto* self = weak.get()) {
        self->do_work(); // 不会执行——get() 返回 nullptr
    } else {
        std::cout << "  Callback skipped: object already destroyed (GOOD)\n\n";
    }
}

int main() {
    test_weak_ptr_survives_owner();
    test_multiple_weak_ptrs();
    test_weak_ptr_in_callback();
    return 0;
}
