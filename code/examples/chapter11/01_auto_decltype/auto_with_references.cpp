// auto_with_references.cpp - auto与引用、const的推导规则
#include <iostream>
#include <vector>
#include <string>

void demonstrate_reference_deduction() {
    std::cout << "=== 引用推导规则演示 ===\n\n";

    int x = 42;
    const int cx = x;
    const int& rx = x;

    // 1. 默认auto丢弃引用和const
    std::cout << "1. 默认auto丢弃引用和const:\n";
    auto a1 = x;     // int
    auto a2 = cx;    // int（丢弃const）
    auto a3 = rx;    // int（丢弃const和引用）

    a1 = 100;        // OK：修改的是新变量
    a2 = 200;        // OK：修改的是新变量
    a3 = 300;        // OK：修改的是新变量

    std::cout << "  x = " << x << " (未被修改)\n";
    std::cout << "  a1 = " << a1 << ", a2 = " << a2 << ", a3 = " << a3 << '\n';

    // 2. auto& 保留引用但丢弃顶层const
    std::cout << "\n2. auto& 保留引用:\n";
    auto& a4 = x;    // int&
    // auto& a5 = cx;   // 错误：不能将非const引用绑定到const
    const auto& a6 = cx;  // const int&（保留const和引用）
    auto& a7 = rx;   // const int&（引用保留，const也被保留因为rx是const引用）

    a4 = 500;        // OK：x被修改
    std::cout << "  a4修改后, x = " << x << '\n';

    // 3. const auto& 最常用：避免拷贝
    std::cout << "\n3. const auto& 避免拷贝:\n";

    std::vector<std::string> strings = {
        "这是一个很长的字符串",
        "另一个很长的字符串",
        "避免拷贝以提高性能"
    };

    std::cout << "  按值拷贝 (auto s) - 会调用拷贝构造:\n";
    for (auto s : strings) {
        std::cout << "    " << s << '\n';
    }

    std::cout << "  const引用 (const auto& s) - 零拷贝:\n";
    for (const auto& s : strings) {
        std::cout << "    " << s << '\n';
    }

    // 4. 修改元素用auto&
    std::cout << "\n4. auto& 修改容器元素:\n";
    std::vector<int> numbers = {1, 2, 3, 4, 5};

    std::cout << "  修改前: ";
    for (auto n : numbers) std::cout << n << ' ';
    std::cout << '\n';

    for (auto& n : numbers) {
        n *= 2;  // 每个元素乘以2
    }

    std::cout << "  修改后: ";
    for (auto n : numbers) std::cout << n << ' ';
    std::cout << '\n';

    // 5. 万能引用（forwarding reference）auto&&
    std::cout << "\n5. auto&& 万能引用:\n";
    auto&& universal1 = x;    // int&（左值）
    auto&& universal2 = 42;   // int&&（右值）

    std::cout << "  auto&& 绑定左值: int&\n";
    std::cout << "  auto&& 绑定右值: int&&\n";
}

void demonstrate_pitfalls() {
    std::cout << "\n=== 常见陷阱演示 ===\n\n";

    // 陷阱1：意外的拷贝
    std::cout << "1. 意外的拷贝:\n";

    struct LargeStruct {
        char data[1024];
        int id;
    };

    LargeStruct large{"test data", 42};

    // 不好：拷贝整个结构体
    auto copy = large;  // 1028字节拷贝

    // 好：使用引用
    const auto& ref = large;  // 零拷贝

    std::cout << "  使用 const auto& 避免拷贝\n";

    // 陷阱2：想修改却没修改
    std::cout << "\n2. 想修改却没修改:\n";
    std::vector<int> v = {1, 2, 3};

    // 错误：auto按值拷贝，修改的是拷贝
    for (auto n : v) {
        n *= 2;  // 不会修改v中的元素
    }
    std::cout << "  错误写法结果: ";
    for (auto n : v) std::cout << n << ' ';
    std::cout << '\n';

    // 正确：使用引用
    for (auto& n : v) {
        n *= 2;
    }
    std::cout << "  正确写法结果: ";
    for (auto n : v) std::cout << n << ' ';
    std::cout << '\n';
}

int main() {
    demonstrate_reference_deduction();
    demonstrate_pitfalls();

    std::cout << "\n=== 演示结束 ===\n";
    return 0;
}
