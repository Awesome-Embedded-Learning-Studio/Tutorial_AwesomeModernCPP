#include <concepts>
#include <type_traits>
#include <string>

template<typename A, typename B>
concept can_add = requires(A a, B b) {
    { a + b } -> std::same_as<std::remove_cvref_t<decltype(a + b)>>;
};

static_assert(can_add<int, int>);
static_assert(can_add<int, double>);
static_assert(can_add<std::string, std::string>);
static_assert(!can_add<int, std::string>);

class MyInt {
    int val;
public:
    MyInt(int v) : val(v) {}
    MyInt operator+(const MyInt& other) const { return MyInt(val + other.val); }
};
static_assert(can_add<MyInt, MyInt>);

class MyFloat {
    float val;
public:
    MyFloat(float v) : val(v) {}
    float get() const { return val; }
};
MyFloat operator+(const MyFloat& a, const MyFloat& b) {
    return MyFloat(a.get() + b.get());
}
static_assert(can_add<MyFloat, MyFloat>);

int main() { return 0; }
