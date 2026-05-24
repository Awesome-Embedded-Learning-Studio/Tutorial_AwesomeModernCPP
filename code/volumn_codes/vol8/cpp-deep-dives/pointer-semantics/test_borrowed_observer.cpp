// 文章 01 测试：Borrowed<T> 和 ObserverPtr<T> 基本用法
// 编译：g++ -std=c++17 -Wall -Wextra -g -o test_borrowed_observer test_borrowed_observer.cpp

#include "borrowed.h"
#include "observer_ptr.h"
#include <iostream>
#include <string>
#include <vector>

void test_borrowed() {
    std::cout << "=== Borrowed<T> tests ===\n";

    int x = 42;
    auto b = borrow(x);
    std::cout << "  borrow(x) = " << b.get() << " (expect 42)\n";

    // 从指针构造（非空）
    Borrowed<int> bp(&x);
    std::cout << "  Borrowed<int>(&x) = " << bp.get() << " (expect 42)\n";

    // const 转换
    Borrowed<const int> bc = bp;
    std::cout << "  const conversion: " << bc.get() << " (expect 42)\n";

    // 函数参数用法
    auto process = [](Borrowed<const std::vector<int>> data) { return data.get().size(); };
    std::vector<int> v{1, 2, 3};
    std::cout << "  process(borrow(v)) = " << process(borrow(v)) << " (expect 3)\n";

    std::cout << "  sizeof(Borrowed<int>) = " << sizeof(Borrowed<int>) << " (expect 8)\n\n";
}

void test_observer_ptr() {
    std::cout << "=== ObserverPtr<T> tests ===\n";

    int a = 10, b = 20;
    auto obs = make_observer(&a);
    std::cout << "  make_observer(&a): " << *obs << " (expect 10)\n";

    // operator bool
    ObserverPtr<int> null_obs;
    std::cout << "  default constructed: " << (null_obs ? "non-null" : "null")
              << " (expect null)\n";
    std::cout << "  with value: " << (obs ? "non-null" : "null") << " (expect non-null)\n";

    // reset and release
    obs.reset(&b);
    std::cout << "  after reset(&b): " << *obs << " (expect 20)\n";
    int* released = obs.release();
    std::cout << "  after release: obs=" << (obs ? "non-null" : "null") << " released=" << *released
              << " (expect null, 20)\n";

    std::cout << "  sizeof(ObserverPtr<int>) = " << sizeof(ObserverPtr<int>) << " (expect 8)\n\n";
}

int main() {
    test_borrowed();
    test_observer_ptr();
    return 0;
}
