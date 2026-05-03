// std::move_only_function (C++23)
// 来源：OnceCallback 前置知识（五）(pre-05)
// 编译：g++ -std=c++23 -Wall -Wextra 10_move_only_function.cpp -o 10_move_only_function

#include <functional>
#include <memory>
#include <iostream>
#include <string>

int add(int a, int b) { return a + b; }

struct Multiplier {
    int operator()(int a, int b) { return a * b; }
};

int main() {
    std::cout << "=== std::function 不支持 move-only ===\n";
    {
        // auto ptr = std::make_unique<int>(42);
        // std::function<int()> f = [p = std::move(ptr)]() { return *p; };
        // 编译错误！unique_ptr 不可拷贝，std::function 要求可拷贝
        std::cout << "  std::function cannot hold move-only callables\n";
    }

    std::cout << "\n=== std::move_only_function 支持 move-only ===\n";
    {
        auto ptr = std::make_unique<int>(42);
        std::move_only_function<int()> f = [p = std::move(ptr)]() { return *p; };
        int result = f();
        std::cout << "  move_only_function with unique_ptr: " << result << "\n";
    }

    std::cout << "\n=== 构造方式 ===\n";
    {
        // 从 lambda 构造
        std::move_only_function<int(int, int)> f1 = [](int a, int b) { return a + b; };

        // 从函数指针构造
        std::move_only_function<int(int, int)> f2 = &add;

        // 从仿函数构造
        std::move_only_function<int(int, int)> f3 = Multiplier{};

        // 默认构造：空的
        std::move_only_function<int()> f4;

        std::cout << "  f1(3, 4) = " << f1(3, 4) << "\n";
        std::cout << "  f2(3, 4) = " << f2(3, 4) << "\n";
        std::cout << "  f3(3, 4) = " << f3(3, 4) << "\n";
        std::cout << "  f4 is " << (f4 ? "not null" : "null") << "\n";
    }

    std::cout << "\n=== 判空与清空 ===\n";
    {
        std::move_only_function<int()> f;
        if (!f) {
            std::cout << "  default-constructed is null\n";
        }

        f = []() { return 42; };
        if (f) {
            std::cout << "  after assignment: not null, f() = " << f() << "\n";
        }

        f = nullptr;
        if (!f) {
            std::cout << "  after nullptr: null again\n";
        }
    }

    std::cout << "\n=== 移动后状态未指定 ===\n";
    {
        std::move_only_function<int()> f = []() { return 42; };
        auto g = std::move(f);
        std::cout << "  g() = " << g() << "\n";
        std::cout << "  f after move: " << (f ? "not null" : "null") << " (unspecified)\n";
    }

    std::cout << "\n=== sizeof 对比 ===\n";
    {
        std::cout << "  sizeof(std::function<void()>):        "
                  << sizeof(std::function<void()>) << " bytes\n";
        std::cout << "  sizeof(std::move_only_function<void()>): "
                  << sizeof(std::move_only_function<void()>) << " bytes\n";
    }

    return 0;
}
