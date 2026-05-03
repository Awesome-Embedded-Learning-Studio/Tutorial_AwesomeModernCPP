// 类型特征（Type Traits）与 if constexpr
// 来源：OnceCallback 前置知识速查 (pre-00)
// 编译：g++ -std=c++17 -Wall -Wextra 06_type_traits.cpp -o 06_type_traits

#include <type_traits>
#include <iostream>
#include <string>

// --- if constexpr 示例 ---

void perform_action() { std::cout << "  action performed\n"; }
int perform_action_int() { return 42; }

template<typename R>
R do_something() {
    if constexpr (std::is_void_v<R>) {
        perform_action();
        return;
    } else {
        return perform_action_int();
    }
}

// --- decltype(auto) 示例 ---

int global_val = 10;

auto f_auto() {
    return global_val;  // 返回 int（丢掉引用）
}

decltype(auto) f_decltype_auto() {
    return global_val;  // 返回 int&（保留引用）
}

// --- Ref-qualified 成员函数 ---

class Widget {
public:
    void process() & {
        std::cout << "  process() &: called on lvalue\n";
    }
    void process() && {
        std::cout << "  process() &&: called on rvalue\n";
    }
};

int main() {
    std::cout << "=== Type Traits ===\n";
    {
        using T1 = std::decay_t<const int&>;
        static_assert(std::is_same_v<T1, int>);
        std::cout << "  decay_t<const int&> = int: OK\n";

        static_assert(std::is_same_v<int, int>);
        static_assert(!std::is_same_v<int, double>);
        std::cout << "  is_same_v checks: OK\n";

        static_assert(std::is_lvalue_reference_v<int&>);
        static_assert(!std::is_lvalue_reference_v<int>);
        static_assert(!std::is_lvalue_reference_v<int&&>);
        std::cout << "  is_lvalue_reference_v checks: OK\n";

        static_assert(std::is_void_v<void>);
        static_assert(!std::is_void_v<int>);
        std::cout << "  is_void_v checks: OK\n";
    }

    std::cout << "\n=== if constexpr ===\n";
    {
        do_something<void>();
        int result = do_something<int>();
        std::cout << "  do_something<int>() = " << result << "\n";
    }

    std::cout << "\n=== decltype(auto) ===\n";
    {
        auto a = f_auto();            // int
        decltype(auto) b = f_decltype_auto();  // int&
        std::cout << "  f_auto() returns int: " << a << "\n";
        std::cout << "  f_decltype_auto() returns int&: " << b << "\n";
        b = 99;
        std::cout << "  after b = 99, global_val = " << global_val << "\n";
    }

    std::cout << "\n=== Ref-qualified 成员函数 ===\n";
    {
        Widget w;
        w.process();            // 调用 & 版本
        std::move(w).process(); // 调用 && 版本
        Widget().process();     // 调用 && 版本
    }

    return 0;
}
