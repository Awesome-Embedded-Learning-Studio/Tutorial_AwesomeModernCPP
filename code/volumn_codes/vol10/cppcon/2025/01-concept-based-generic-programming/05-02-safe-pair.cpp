#include <iostream>
#include <type_traits>

template<typename T, typename U>
constexpr bool is_non_narrowing_v = false;

template<>
constexpr bool is_non_narrowing_v<int, double> = true;

template<>
constexpr bool is_non_narrowing_v<int, int> = true;

template<>
constexpr bool is_non_narrowing_v<double, double> = true;

template<>
constexpr bool is_non_narrowing_v<double, int> = false;

template<typename T1, typename T2>
class SafePair {
public:
    T1 first;
    T2 second;
    SafePair() : first{}, second{} {}
    SafePair(T1 f, T2 s) : first(f), second(s) {}

    // Converting constructor: SafePair<U1,U2> -> SafePair<T1,T2>
    // Only enabled when both U1->T1 and U2->T2 are non-narrowing
    template<typename U1, typename U2>
        requires (is_non_narrowing_v<U1, T1> && is_non_narrowing_v<U2, T2>)
    SafePair(const SafePair<U1, U2>& other)
        : first(static_cast<T1>(other.first))
        , second(static_cast<T2>(other.second))
    {}
};

int main() {
    SafePair<int, int> src2{3, 2};
    SafePair<double, double> dst2(src2);
    std::cout << dst2.first << ", " << dst2.second << std::endl;
    // SafePair<double, double> src{3.14, 2.718};
    // SafePair<int, int> dst = src;  // This would fail to compile!
    return 0;
}
