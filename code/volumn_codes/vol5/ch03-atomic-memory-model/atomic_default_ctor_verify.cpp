/*
 * 验证：std::atomic<T> 不要求 T 必须 default-constructible
 *
 * 背景：文章原文错误声称 T 必须 default-constructible，
 *       但标准只要求 T 是 trivially copyable。
 *       使用删除了默认构造函数的类型来验证。
 *
 * 预期结果：编译通过，运行正常，输出 42 100 200
 *
 * 编译命令：
 *   g++ -std=c++20 -O2 -o /tmp/atomic_default_ctor_verify atomic_default_ctor_verify.cpp
 *
 * 运行：
 *   /tmp/atomic_default_ctor_verify
 *
 * 参考资料：
 *   - https://en.cppreference.com/w/cpp/atomic/atomic
 *
 * 编译器：GCC 16.1.1
 */

#include <atomic>
#include <iostream>

struct NonDefault {
    int x;
    NonDefault() = delete;
    NonDefault(int v) : x(v) {}
};

static_assert(std::is_trivially_copyable_v<NonDefault>, "NonDefault should be trivially copyable");

int main() {
    // 用 atomic(T) 构造函数，不使用默认构造函数
    std::atomic<NonDefault> a{NonDefault{42}};

    auto val = a.load();
    std::cout << val.x << "\n";

    a.store(NonDefault{100});
    val = a.load();
    std::cout << val.x << "\n";

    // CAS 操作
    NonDefault expected{100};
    bool ok = a.compare_exchange_strong(expected, NonDefault{200});
    std::cout << a.load().x << "\n";

    return 0;
}
