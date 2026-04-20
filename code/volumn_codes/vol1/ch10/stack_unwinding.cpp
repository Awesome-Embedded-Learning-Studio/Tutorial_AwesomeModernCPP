#include <iostream>
#include <stdexcept>

struct Trace {
    const char* name_;
    explicit Trace(const char* n) : name_(n)
    { std::cout << "  Constructing: " << name_ << "\n"; }
    ~Trace()
    { std::cout << "  Destroying: " << name_ << "\n"; }
};

void inner()
{
    Trace t3("t3_in_inner");
    throw std::runtime_error("boom from inner");
}

void middle()
{
    Trace t2("t2_in_middle");
    inner();
}

int main()
{
    try {
        Trace t1("t1_in_main");
        middle();
    }
    catch (const std::exception& e) {
        std::cout << "  Caught: " << e.what() << "\n";
    }
    return 0;
}
