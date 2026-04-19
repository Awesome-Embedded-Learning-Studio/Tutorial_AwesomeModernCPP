// Lambda Return Type Deduction
// Demonstrates automatic return type deduction in lambda expressions

#include <iostream>
#include <cmath>

int main() {
    std::cout << "=== Lambda Type Deduction Demo ===" << std::endl;

    // Automatic deduction for single return statement
    auto square = [](int x) { return x * x; };
    std::cout << "square(5) = " << square(5) << " (type: int)" << std::endl;

    // Deduction to double
    auto divide = [](int a, int b) {
        return static_cast<double>(a) / b;
    };
    std::cout << "divide(7, 2) = " << divide(7, 2) << " (type: double)" << std::endl;

    // Explicit return type for complex logic
    auto compute = [](int x) -> double {
        if (x > 0) {
            return std::sqrt(x);
        } else {
            return 0.0;
        }
    };
    std::cout << "compute(16) = " << compute(16) << std::endl;
    std::cout << "compute(-4) = " << compute(-4) << std::endl;

    // Lambda returning different types based on condition
    auto conditional = [](bool flag) {
        if (flag) {
            return 42.0;    // double
        } else {
            return 3.14;    // double - common type is double, int promotes to double
        }
    };
    std::cout << "conditional(true) = " << conditional(true) << std::endl;
    std::cout << "conditional(false) = " << conditional(false) << std::endl;

    // decltype(auto) for perfect forwarding (C++14)
    auto identity = [](auto&& x) -> decltype(auto) {
        return std::forward<decltype(x)>(x);
    };
    int value = 100;
    int& ref = identity(value);
    ref = 200;
    std::cout << "After identity modification, value = " << value << std::endl;

    return 0;
}
