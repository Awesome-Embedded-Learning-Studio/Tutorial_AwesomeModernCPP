// Lambda Expression Basic Syntax
// Demonstrates the fundamental structure and syntax of C++ lambda expressions

#include <iostream>
#include <functional>

int main() {
    std::cout << "=== Lambda Basic Syntax Demo ===" << std::endl;

    // Lambda syntax: [capture](parameters) -> return_type { body }

    // Simplest lambda: does nothing
    auto do_nothing = []() {};
    do_nothing();
    std::cout << "Empty lambda executed" << std::endl;

    // Lambda with simple return
    auto return_42 = []() { return 42; };
    std::cout << "return_42() = " << return_42() << std::endl;

    // Lambda with parameter
    auto add = [](int a, int b) { return a + b; };
    int result = add(3, 4);
    std::cout << "add(3, 4) = " << result << std::endl;

    // Lambda with explicit return type
    auto complex = [](int x) -> int {
        if (x > 0) {
            return x * 2;
        } else {
            return x;
        }
    };
    std::cout << "complex(5) = " << complex(5) << std::endl;
    std::cout << "complex(-3) = " << complex(-3) << std::endl;

    // Lambda stored in std::function (has runtime overhead)
    std::function<int(int, int)> func = add;
    std::cout << "func(10, 20) = " << func(10, 20) << std::endl;

    // Using template parameters for zero overhead
    auto call_func = [](auto f) {
        return f(100, 200);
    };
    std::cout << "call_func(add) = " << call_func(add) << std::endl;

    return 0;
}
