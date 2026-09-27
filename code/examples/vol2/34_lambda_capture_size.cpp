#include <iostream>

void demo_closure_size() {
    int a = 0;
    double b = 0.0;
    int& ref = a;

    auto no_capture = []() {};
    auto capture_int = [a]() { return a; };
    auto capture_ref = [&a]() { return a; };
    auto capture_both = [a, &b]() { return a + b; };

    std::cout << "no_capture:    " << sizeof(no_capture) << " bytes\n";
    // 通常 1 byte（空类特例）

    std::cout << "capture_int:   " << sizeof(capture_int) << " bytes\n";
    // 通常 4 bytes（一个 int）

    std::cout << "capture_ref:   " << sizeof(capture_ref) << " bytes\n";
    // 通常 8 bytes（一个指针，64 位系统）

    std::cout << "capture_both:  " << sizeof(capture_both) << " bytes\n";
    // 通常 16 bytes（int + double 引用/指针，考虑对齐）
}

int main() {
    demo_closure_size();
    return 0;
}
