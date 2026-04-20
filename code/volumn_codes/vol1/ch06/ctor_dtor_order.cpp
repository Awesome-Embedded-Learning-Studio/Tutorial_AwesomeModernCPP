#include <iostream>

struct Tracer {
    const char* name;
    explicit Tracer(const char* n) : name(n) {
        std::cout << "  [" << name << "] constructed" << std::endl;
    }
    ~Tracer() {
        std::cout << "  [" << name << "] destructed" << std::endl;
    }
};

struct Container {
    Tracer member_a;
    Tracer member_b;
    Container() : member_a("member_a"), member_b("member_b") {
        std::cout << "  [Container] ctor body" << std::endl;
    }
    ~Container() {
        std::cout << "  [Container] dtor body" << std::endl;
    }
};

int main() {
    std::cout << "=== begin ===" << std::endl;
    {
        Tracer local("local");
        Container container;
        Tracer* heap = new Tracer("heap");
        delete heap;
    }
    std::cout << "=== end ===" << std::endl;
}
