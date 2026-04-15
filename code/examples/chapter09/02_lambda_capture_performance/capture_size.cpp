// Lambda Object Size Analysis
// Demonstrates how different captures affect lambda object size

#include <iostream>
#include <memory>
#include <string>
#include <array>

struct BigObject {
    int data[100];
};

int main() {
    std::cout << "=== Lambda Object Size Analysis ===" << std::endl;

    // Empty lambda
    auto empty = []() {};
    std::cout << "Empty lambda size: " << sizeof(empty) << " bytes" << std::endl;

    // Capture by value - single int
    int x = 42;
    auto capture_one_int = [x]() { return x; };
    std::cout << "Lambda [x] size: " << sizeof(capture_one_int) << " bytes" << std::endl;

    // Capture multiple values
    int a = 1, b = 2, c = 3;
    auto capture_three = [a, b, c]() { return a + b + c; };
    std::cout << "Lambda [a, b, c] size: " << sizeof(capture_three) << " bytes" << std::endl;

    // Capture reference (pointer size)
    auto capture_ref = [&x]() { return x; };
    std::cout << "Lambda [&x] size: " << sizeof(capture_ref) << " bytes (pointer)" << std::endl;

    // Capture large object
    BigObject big;
    auto capture_big = [big]() { return big.data[0]; };
    std::cout << "Lambda [BigObject] size: " << sizeof(capture_big) << " bytes" << std::endl;
    std::cout << "BigObject size: " << sizeof(BigObject) << " bytes" << std::endl;

    // Capture string
    std::string str = "Hello";
    auto capture_string = [str]() { return str; };
    std::cout << "Lambda [string] size: " << sizeof(capture_string) << " bytes" << std::endl;

    // Capture unique_ptr
    auto ptr = std::make_unique<int>(42);
    auto capture_ptr = [p = std::move(ptr)]() { return *p; };
    std::cout << "Lambda [unique_ptr] size: " << sizeof(capture_ptr) << " bytes" << std::endl;

    // Reference vs value size comparison
    std::cout << "\n=== Reference vs Value Size ===" << std::endl;

    int value = 100;
    auto by_value = [value]() {};
    auto by_ref = [&value]() {};

    std::cout << "By value size: " << sizeof(by_value) << " bytes" << std::endl;
    std::cout << "By ref size:   " << sizeof(by_ref) << " bytes (pointer)" << std::endl;

    // Alignment considerations
    std::cout << "\n=== Alignment Effects ===" << std::endl;

    char c = 'x';
    double d = 3.14;
    auto mixed = [c, d]() {};
    std::cout << "Mixed [char, double] size: " << sizeof(mixed) << " bytes" << std::endl;
    std::cout << "char size: " << sizeof(c) << " bytes" << std::endl;
    std::cout << "double size: " << sizeof(d) << " bytes" << std::endl;

    // Capture with padding
    auto multi_char = [c, c, c, c, c]() {};
    std::cout << "Five chars size: " << sizeof(multi_char) << " bytes" << std::endl;

    // Array capture
    std::array<int, 5> arr = {1, 2, 3, 4, 5};
    auto capture_array = [arr]() { return arr[0]; };
    std::cout << "Lambda [array<5>] size: " << sizeof(capture_array) << " bytes" << std::endl;

    // Practical embedded considerations
    std::cout << "\n=== Embedded Considerations ===" << std::endl;
    std::cout << "For callbacks stored in arrays or structs:" << std::endl;
    std::cout << "- Prefer value capture for small types" << std::endl;
    std::cout << "- Use reference capture for large objects (careful with lifetime)" << std::endl;
    std::cout << "- Init capture can move large objects efficiently" << std::endl;

    // Demonstrating zero overhead of empty lambda
    std::cout << "\n=== Compiler Optimization ===" << std::endl;
    std::cout << "Compilers optimize empty lambdas to minimal size" << std::endl;
    std::cout << "With -O2, capture overhead may be eliminated through inlining" << std::endl;

    return 0;
}
