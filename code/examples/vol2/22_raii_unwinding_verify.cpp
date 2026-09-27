// GCC 16.1.1, -O2 -std=c++11
#include <iostream>
#include <stdexcept>

struct Tracer {
    const char* name;
    explicit Tracer(const char* n) : name(n) {
        std::cout << "Tracer(" << name << ") constructed\n";
    }
    ~Tracer() { std::cout << "~Tracer(" << name << ") destroyed\n"; }
};

void may_throw() {
    throw std::runtime_error("Exception thrown");
}

void test_stack_unwinding() {
    Tracer t1("t1");
    Tracer t2("t2");
    may_throw();     // 异常在这里抛出
    Tracer t3("t3"); // 永远不会执行到这里
}

int main() {
    try {
        test_stack_unwinding();
    } catch (const std::exception& e) {
        std::cout << "Caught: " << e.what() << "\n";
    }
}
