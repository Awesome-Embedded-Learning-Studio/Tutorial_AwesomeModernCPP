// decltype_basics.cpp - decltype关键字的基础用法
#include <iostream>
#include <vector>
#include <type_traits>
#include <utility>

void basic_decltype() {
    std::cout << "=== decltype基础演示 ===\n\n";

    int x = 42;
    const int cx = x;
    const int& rx = x;
    int* px = &x;

    // 1. decltype保留类型精确信息
    std::cout << "1. decltype保留类型:\n";
    decltype(x) a = x;        // int
    decltype(cx) b = cx;      // const int
    decltype(rx) c = rx;      // const int&
    decltype(px) d = px;      // int*

    std::cout << "  decltype(x) = int: " << std::is_same_v<decltype(x), int> << '\n';
    std::cout << "  decltype(cx) = const int: " << std::is_same_v<decltype(cx), const int> << '\n';
    std::cout << "  decltype(rx) = const int&: " << std::is_same_v<decltype(rx), const int&> << '\n';

    // 2. decltype((variable)) 双括号规则
    std::cout << "\n2. 双括号规则:\n";
    decltype((x)) e = x;      // int&（注意：是引用！）
    decltype((cx)) f = cx;   // const int&（cx是const int，所以decltype((cx))是const int&）

    std::cout << "  decltype((x)) 是引用: " << std::is_reference_v<decltype((x))> << '\n';
    std::cout << "  decltype(x) 不是引用: " << std::is_reference_v<decltype(x)> << '\n';

    // 3. decltype用于表达式
    std::cout << "\n3. decltype用于表达式:\n";
    int y = 10;
    decltype(x + y) sum = x + y;  // int（表达式结果是int）
    decltype(x = y) assign = x;   // int&（赋值表达式返回左值引用）

    std::cout << "  decltype(x + y) = int: " << std::is_same_v<decltype(x + y), int> << '\n';
    std::cout << "  decltype(x = y) = int&: " << std::is_same_v<decltype(x = y), int&> << '\n';

    // 4. 顶层const vs 底层const
    std::cout << "\n4. 顶层const vs 底层const:\n";
    const int* p1 = &x;   // 底层const（指向的内容是const）
    int* const p2 = &x;   // 顶层const（指针本身是const）

    decltype(p1) p3 = p1;  // const int*（保留底层const）
    decltype(p2) p4 = p2;  // int* const（保留顶层const）

    std::cout << "  decltype(p1) 保留底层const\n";
    std::cout << "  decltype(p2) 保留顶层const\n";
}

void decltype_use_cases() {
    std::cout << "\n=== decltype实际应用 ===\n\n";

    // 1. 定义与表达式类型相同的变量
    std::cout << "1. 定义相同类型的变量:\n";
    std::vector<int> v = {1, 2, 3, 4, 5};

    // 使用decltype获取迭代器类型
    decltype(v.begin()) it = v.begin();
    *it = 100;

    std::cout << "  第一个元素: " << v[0] << '\n';

    // 2. 类型别名
    std::cout << "\n2. 类型别名:\n";
    using VecIterator = decltype(v.begin());
    using ValueType = decltype(v)::value_type;

    VecIterator it2 = v.begin();
    ValueType value = v[0];

    std::cout << "  值类型: " << value << '\n';

    // 3. 用于返回类型（C++11风格）
    std::cout << "\n3. 函数返回类型:\n";
    auto add = [](int a, int b) -> decltype(a + b) {
        return a + b;
    };

    std::cout << "  add(3, 4) = " << add(3, 4) << '\n';

    // 4. 模板中的decltype
    std::cout << "\n4. 模板中的类型推导:\n";
    auto multiply = [](auto a, auto b) -> decltype(a * b) {
        return a * b;
    };

    std::cout << "  multiply(3, 4) = " << multiply(3, 4) << '\n';
    std::cout << "  multiply(3.5, 2) = " << multiply(3.5, 2) << '\n';
}

void auto_vs_decltype() {
    std::cout << "\n=== auto vs decltype对比 ===\n\n";

    int x = 42;
    const int& cref = x;

    // auto丢弃const和引用
    auto a = cref;       // int
    a = 100;             // OK

    // decltype保留const和引用
    decltype(cref) b = cref;  // const int&
    // b = 100;             // 错误：不能修改const

    std::cout << "  auto 推导: int\n";
    std::cout << "  decltype 推导: const int&\n";

    std::cout << "\n  修改a: a = " << a << " (x未被修改)\n";
    std::cout << "  b绑定到x: b = " << b << '\n';

    // decltype((x))规则
    std::cout << "\n  decltype((x))规则:\n";
    decltype((x)) c = x;  // int&
    c = 200;              // OK：x被修改
    std::cout << "  修改c后, x = " << x << '\n';
}

int main() {
    basic_decltype();
    decltype_use_cases();
    auto_vs_decltype();

    std::cout << "\n=== 演示结束 ===\n";
    return 0;
}
