#include <iostream>
#include <type_traits>

template<typename T>
struct number {
    T value;
    explicit operator T() const { return value; }
};
template<typename T> number(T) -> number<T>;
template<typename T> using number_of = number<T>;

int main() {
    number_of a{42};
    static_assert(std::is_same_v<decltype(a), number<int>>);
    number_of b{3u};
    static_assert(std::is_same_v<decltype(b), number<unsigned int>>);
    number_of c{2.718};
    static_assert(std::is_same_v<decltype(c), number<double>>);
    std::cout << a.value << "\n" << b.value << "\n" << c.value << "\n";
    return 0;
}
