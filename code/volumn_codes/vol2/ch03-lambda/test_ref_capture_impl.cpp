/**
 * @file test_ref_capture_impl.cpp
 * @brief 验证引用捕获的底层实现（存储指针/引用）
 *
 * 编译命令:
 *   g++ -std=c++20 -O0 -o test_ref_capture_impl test_ref_capture_impl.cpp
 *
 * 运行:
 *   ./test_ref_capture_impl
 */

#include <iostream>

void demo_ref_capture() {
    int sum = 0;

    auto accumulate = [&sum](int value) {
        sum += value;   // 通过引用修改外部变量
    };

    accumulate(10);
    accumulate(20);
    accumulate(30);

    std::cout << "引用捕获测试: sum = " << sum << " (期望: 60)\n";
}

void test_ref_capture_size() {
    int a = 0;
    double b = 0.0;

    auto capture_ref_int = [&a]() { return a; };
    auto capture_ref_double = [&b]() { return b; };
    auto capture_two_refs = [&a, &b]() { return a + b; };

    std::cout << "\n=== 引用捕获的大小 ===\n";
    std::cout << "引用捕获 int:     " << sizeof(capture_ref_int) << " bytes\n";
    std::cout << "引用捕获 double:  " << sizeof(capture_ref_double) << " bytes\n";
    std::cout << "捕获两个引用:     " << sizeof(capture_two_refs) << " bytes\n";
    std::cout << "指针大小:         " << sizeof(int*) << " bytes\n";
}

// 验证 const operator() 中可以修改引用捕获的变量
struct TestRefCaptureConst {
    int& ref;
    void operator()(int value) const {
        // ref 本身是 const（不能重新绑定），但 ref 指向的对象可以修改
        ref += value;
    }
};

void test_ref_constness() {
    int x = 0;
    TestRefCaptureConst t{x};
    t(10);

    std::cout << "\n=== const operator() 测试 ===\n";
    std::cout << "通过 const operator() 修改后: x = " << x << " (期望: 10)\n";
    std::cout << "结论: 引用捕获的 const 语义类似于 int* const ptr\n";
}

int main() {
    demo_ref_capture();
    test_ref_capture_size();
    test_ref_constness();

    std::cout << "\n=== 验证结论 ===\n";
    std::cout << "1. 引用捕获在底层存储指针（64 位系统上为 8 字节）\n";
    std::cout << "2. const operator() 不阻止修改引用指向的对象\n";
    std::cout << "3. 引用捕获的 const 语义是「引用本身不可重新绑定」，而非「对象不可修改」\n";

    return 0;
}
