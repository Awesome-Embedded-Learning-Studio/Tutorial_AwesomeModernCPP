#include <cstdlib>
#include <iostream>

struct Tracer {
    const char* name;
    explicit Tracer(const char* n) : name(n) {
        std::cout << "Tracer(" << name << ") constructed\n";
    }
    ~Tracer() { std::cout << "~Tracer(" << name << ") destroyed\n"; }
};

void test_normal_return() {
    Tracer t("normal");
    return; // 析构函数会被调用
}

void test_exit() {
    Tracer t("exit");
    std::exit(0); // 析构函数不会被调用！
}

int main() {
    std::cout << "Normal case:\n";
    test_normal_return();
    std::cout << "\nstd::exit() case:\n";
    test_exit(); // 内部构造 Tracer("exit") 后 std::exit，进程直接终止
}
