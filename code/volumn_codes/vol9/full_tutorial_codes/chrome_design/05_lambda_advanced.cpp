// Lambda 高级特性：mutable、初始化捕获、C++17/C++20 bind
// 来源：OnceCallback 前置知识（三）(pre-03)
// 编译：g++ -std=c++20 -Wall -Wextra 05_lambda_advanced.cpp -o 05_lambda_advanced

#include <memory>
#include <string>
#include <tuple>
#include <functional>
#include <iostream>

int main() {
    std::cout << "=== mutable lambda ===\n";
    {
        int x = 10;

        auto f2 = [x]() mutable {
            x++;
            return x;
        };
        std::cout << "  first call: " << f2() << "\n";   // 11
        std::cout << "  second call: " << f2() << "\n";   // 12
    }

    std::cout << "\n=== 初始化捕获 (C++14) ===\n";
    {
        auto ptr = std::make_unique<int>(42);
        auto f1 = [p = std::move(ptr)]() { return *p; };  // 移动捕获
        std::cout << "  move capture: " << f1() << "\n";

        std::string s = "hello";
        auto f2 = [len = s.size()]() { return len; };      // 存储计算结果
        std::cout << "  computed capture: " << f2() << "\n";

        auto f3 = [counter = 0]() mutable { return ++counter; };  // 新变量
        std::cout << "  new var capture: " << f3() << ", " << f3() << "\n";
    }

    std::cout << "\n=== 旧版 bind (C++17): tuple + apply ===\n";
    {
        auto add = [](int a, int b, int c) { return a + b + c; };

        auto bind_old = [](auto&& f, auto&&... args) {
            return [f = std::forward<decltype(f)>(f),
                    tup = std::make_tuple(std::forward<decltype(args)>(args)...)]
                (auto&&... call_args) mutable -> decltype(auto) {
                return std::apply([&](auto&... bound) -> decltype(auto) {
                    return f(bound..., std::forward<decltype(call_args)>(call_args)...);
                }, tup);
            };
        };

        auto bound = bind_old(add, 10, 20);
        std::cout << "  bind_old(10, 20)(30) = " << bound(30) << "\n";
    }

    std::cout << "\n=== 新版 bind (C++20): capture pack expansion ===\n";
    {
        auto bind_new = [](auto&& f, auto&&... args) {
            return [f = std::forward<decltype(f)>(f),
                    ...bound = std::forward<decltype(args)>(args)]
                (auto&&... call_args) mutable -> decltype(auto) {
                return std::invoke(std::move(f),
                                  std::move(bound)...,
                                  std::forward<decltype(call_args)>(call_args)...);
            };
        };

        auto add = [](int a, int b, int c) { return a + b + c; };
        auto bound = bind_new(add, 10, 20);
        std::cout << "  bind_new(10, 20)(30) = " << bound(30) << "\n";
    }

    return 0;
}
