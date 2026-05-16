#include <iostream>
#include <concepts>
#include <string>

template<int N>
concept IsSmall = (N <= 10);

template<int N>
concept IsLarge = (N > 10);

template<int N> requires IsSmall<N>
std::string describe_size() { return "small: " + std::to_string(N); }

template<int N> requires IsLarge<N>
std::string describe_size() { return "LARGE: " + std::to_string(N); }

int main() {
    std::cout << describe_size<3>() << "\n";
    std::cout << describe_size<50>() << "\n";
    return 0;
}
