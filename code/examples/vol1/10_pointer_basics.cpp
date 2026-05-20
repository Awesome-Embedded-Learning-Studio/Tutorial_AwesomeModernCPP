// 10_pointer_basics.cpp
// 指针基础综合演练：取地址、解引用、指针 swap、空指针安全检查

#include <iostream>

void swap_by_pointer(int* a, int* b) {
    if (a == nullptr || b == nullptr) {
        return;
    }
    int temp = *a;
    *a = *b;
    *b = temp;
}

void safe_print(const char* label, const int* p) {
    std::cout << label;
    if (p != nullptr) {
        std::cout << *p << " (地址: " << p << ")" << std::endl;
    } else {
        std::cout << "(空指针)" << std::endl;
    }
}

int main() {
    int x = 42;
    int* p = &x;
    std::cout << "=== 取地址与解引用 ===" << std::endl;
    std::cout << "x = " << x << ", &x = " << &x << std::endl;
    std::cout << "p = " << p << ", *p = " << *p << std::endl;

    *p = 100;
    std::cout << "\n=== *p = 100 后 ===" << std::endl;
    std::cout << "x = " << x << std::endl;

    int a = 10, b = 20;
    std::cout << "\n=== swap ===" << std::endl;
    std::cout << "交换前: a=" << a << ", b=" << b << std::endl;
    swap_by_pointer(&a, &b);
    std::cout << "交换后: a=" << a << ", b=" << b << std::endl;

    std::cout << "\n=== 空指针 ===" << std::endl;
    int value = 99;
    safe_print("有效指针: ", &value);
    safe_print("空指针:   ", static_cast<int*>(nullptr));

    return 0;
}
