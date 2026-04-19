// Generic Lambda (C++14)
// Demonstrates lambda expressions with auto parameters (template-like behavior)

#include <iostream>
#include <cstdint>
#include <string>

int main() {
    std::cout << "=== Generic Lambda Demo (C++14) ===" << std::endl;

    // Generic lambda: can accept any addable types
    auto add = [](auto a, auto b) {
        return a + b;
    };

    int int_result = add(3, 4);
    std::cout << "add(3, 4) = " << int_result << " (int)" << std::endl;

    double double_result = add(3.5, 2.5);
    std::cout << "add(3.5, 2.5) = " << double_result << " (double)" << std::endl;

    std::string concat = add(std::string("Hello, "), std::string("World!"));
    std::cout << "add(\"Hello, \", \"World!\") = " << concat << " (string)" << std::endl;

    // Generic lambda for register operations
    std::cout << "\n=== Generic Register Write ===" << std::endl;

    auto write_reg = [](auto addr, auto value) {
        std::cout << "Write 0x" << std::hex << static_cast<unsigned long>(value)
                  << " to address 0x" << static_cast<unsigned long>(addr)
                  << std::dec << std::endl;
    };

    write_reg(0x40000000u, uint32_t(0x12345678));
    write_reg(0x50000000u, uint16_t(0xABCD));

    // Generic lambda with multiple auto parameters
    auto print_pair = [](auto first, auto second) {
        std::cout << "(" << first << ", " << second << ")" << std::endl;
    };

    print_pair(42, "answer");
    print_pair(3.14, "pi");
    print_pair('A', 65);

    // Generic lambda with perfect forwarding
    auto forward_and_print = [](auto&& x) -> decltype(auto) {
        std::cout << "Value: " << x << std::endl;
        return std::forward<decltype(x)>(x);
    };

    int value = 100;
    int& ref = forward_and_print(value);
    std::cout << "Original value address: " << &value << std::endl;
    std::cout << "Returned reference address: " << &ref << std::endl;

    // Generic lambda with conditional compilation
    auto process = [](auto x) {
        if constexpr (std::is_integral_v<decltype(x)>) {
            std::cout << "Processing integer: " << x << std::endl;
        } else if constexpr (std::is_floating_point_v<decltype(x)>) {
            std::cout << "Processing float: " << x << std::endl;
        } else {
            std::cout << "Processing other type" << std::endl;
        }
    };

    process(42);
    process(3.14);
    process("hello");

    return 0;
}
