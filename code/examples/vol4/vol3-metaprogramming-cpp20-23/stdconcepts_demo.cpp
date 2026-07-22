// 标准库 <concepts> 常用概念实测:same_as / convertible_to / derived_from / common_with / integral /
// floating_point 对应文章:documents/vol4-advanced/vol3-metaprogramming-cpp20-23/01-concepts.md
// 编译运行:g++ -Wall -Wextra -std=c++20 stdconcepts_demo.cpp -o stdc && ./stdc
#include <concepts>
#include <iostream>
#include <vector>

struct Base {};
struct Derived : Base {};
struct Unrelated {};

int main() {
    std::cout << std::boolalpha;
    std::cout << "same_as<int,int>:             " << std::same_as<int, int> << "\n";
    std::cout << "same_as<int, const int>:      " << std::same_as<int, const int> << "\n";
    std::cout << "convertible_to<int,double>:   " << std::convertible_to<int, double> << "\n";
    std::cout << "convertible_to<double,int>:   " << std::convertible_to<double, int> << "\n";
    std::cout << "derived_from<Derived,Base>:   " << std::derived_from<Derived, Base> << "\n";
    std::cout << "derived_from<Unrelated,Base>: " << std::derived_from<Unrelated, Base> << "\n";
    std::cout << "common_with<int,double>:      " << std::common_with<int, double> << "\n";
    std::cout << "default_initializable<int>:   " << std::default_initializable<int> << "\n";
    std::cout << "integral<int>:                " << std::integral<int> << "\n";
    std::cout << "integral<bool>:               " << std::integral<bool> << "\n";
    std::cout << "floating_point<float>:        " << std::floating_point<float> << "\n";
    return 0;
}
