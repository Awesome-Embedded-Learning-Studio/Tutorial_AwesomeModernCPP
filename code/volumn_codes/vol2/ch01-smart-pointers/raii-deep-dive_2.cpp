#include <iostream>
#include <stdexcept>

struct Tracer {
    explicit Tracer(const char* name) : name_(name) {
        std::cout << "Tracer(" << name_ << ") 构造\n";
    }
    ~Tracer() noexcept {
        std::cout << "~Tracer(" << name_ << ") 析构\n";
    }
    Tracer(const Tracer&) = delete;
    Tracer& operator=(const Tracer&) = delete;
private:
    const char* name_;
};

void demo_stack_unwinding() {
    Tracer a("a");
    Tracer b("b");
    throw std::runtime_error("boom!");
    Tracer c("c");  // 永远不会执行到这里
}

int main() {
    try {
        demo_stack_unwinding();
    } catch (const std::exception& e) {
        std::cout << "捕获异常: " << e.what() << "\n";
    }
}
