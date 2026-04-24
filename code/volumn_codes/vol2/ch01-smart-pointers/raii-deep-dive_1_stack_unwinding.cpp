// GCC 13, -O2 -std=c++11
#include <iostream>
#include <stdexcept>

/// @brief 验证栈展开保证：异常抛出时局部对象的析构函数会被调用
struct Tracer {
    const char* name;

    explicit Tracer(const char* n) : name(n) {
        std::cout << "Tracer(" << name << ") constructed\n";
    }

    ~Tracer() {
        std::cout << "~Tracer(" << name << ") destroyed\n";
    }
};

void may_throw() {
    throw std::runtime_error("Exception thrown");
}

void test_stack_unwinding() {
    Tracer t1("t1");
    Tracer t2("t2");

    may_throw();  // 异常在这里抛出

    Tracer t3("t3");  // 永远不会执行到这里
}

int main() {
    std::cout << "=== Testing stack unwinding guarantee ===\n";
    try {
        test_stack_unwinding();
    } catch (const std::exception& e) {
        std::cout << "Caught: " << e.what() << "\n";
        std::cout << "Stack unwinding completed successfully\n";
    }
    return 0;
}

// 运行输出：
// === Testing stack unwinding guarantee ===
// Tracer(t1) constructed
// Tracer(t2) constructed
// ~Tracer(t2) destroyed
// ~Tracer(t1) destroyed
// Caught: Exception thrown
// Stack unwinding completed successfully
