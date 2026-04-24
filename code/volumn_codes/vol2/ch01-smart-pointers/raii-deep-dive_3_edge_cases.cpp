// GCC 13, -O2 -std=c++11
#include <iostream>
#include <cstdlib>
#include <stdexcept>

/// @brief 验证析构函数不被调用的情况：std::exit() 和 std::abort()
struct Tracer {
    const char* name;

    explicit Tracer(const char* n) : name(n) {
        std::cout << "Tracer(" << name << ") constructed\n";
    }

    ~Tracer() {
        std::cout << "~Tracer(" << name << ") destroyed\n";
    }
};

void test_normal_return() {
    Tracer t("in_test_normal");
    std::cout << "Normal return\n";
    return;  // 析构函数会被调用
}

void test_exit() {
    Tracer t("in_test_exit");
    std::cout << "Calling std::exit(0)\n";
    std::cout << "Note: std::exit() does NOT unwind stack\n";
    std::exit(0);  // 析构函数不会被调用！
}

void test_abort() {
    Tracer t("in_test_abort");
    std::cout << "Calling std::abort()\n";
    std::abort();  // 析构函数不会被调用！
}

int main() {
    std::cout << "=== Test 1: Normal return ===\n";
    test_normal_return();
    std::cout << "After test_normal_return\n\n";

    std::cout << "=== Test 2: std::exit() ===\n";
    test_exit();
    std::cout << "This line never reached\n";

    return 0;
}

// 运行输出：
// === Test 1: Normal return ===
// Tracer(in_test_normal) constructed
// Normal return
// ~Tracer(in_test_normal) destroyed
// After test_normal_return
//
// === Test 2: std::exit() ===
// Note: std::exit() does NOT unwind stack
// Tracer(in_test_exit) constructed
// Calling std::exit(0)
// (程序终止，析构函数未被调用)
