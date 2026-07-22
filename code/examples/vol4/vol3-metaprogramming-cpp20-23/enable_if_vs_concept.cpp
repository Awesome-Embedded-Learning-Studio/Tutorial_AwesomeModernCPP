// enable_if vs concept 的报错对比:同一个 add,两种约束方式,故意传错类型看报错差异
// 对应文章:documents/vol4-advanced/vol3-metaprogramming-cpp20-23/01-concepts.md
//
// 默认编译(演示两个 add 都能正常工作):
//   g++ -Wall -Wextra -std=c++20 enable_if_vs_concept.cpp -o eic && ./eic
//
// 复现报错对比(分别加宏编译,看 concept 报错如何直接点名约束、enable_if 版暴露 SFINAE 内部):
//   g++ -std=c++20 -DDEMO_OLD enable_if_vs_concept.cpp    # enable_if 版报错
//   g++ -std=c++20 -DDEMO_NEW enable_if_vs_concept.cpp    # concept 版报错(constraints not
//   satisfied)
#include <concepts>
#include <iostream>
#include <string>
#include <type_traits>

namespace old_way {
// C++17 的 enable_if:把约束塞进额外的默认模板参数
template <typename T,
          typename = std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T>>>
T add(T a, T b) {
    return a + b;
}
} // namespace old_way

namespace new_way {
// C++20 concept:约束写进签名,有名字、可复用
template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template <typename T>
    requires Numeric<T>
T add(T a, T b) {
    return a + b;
}
} // namespace new_way

int main() {
    std::cout << "old_way::add(2, 3) = " << old_way::add(2, 3) << "\n";
    std::cout << "new_way::add(2, 3) = " << new_way::add(2, 3) << "\n";

#if DEMO_OLD
    std::string s1 = "a", s2 = "b";
    old_way::add(s1, s2); // 故意传错:看 enable_if 版报错(no type named 'type' in enable_if<false>)
#elif DEMO_NEW
    std::string s1 = "a", s2 = "b";
    new_way::add(s1, s2); // 故意传错:看 concept 版报错(constraints not satisfied -> Numeric<T>)
#endif
    return 0;
}
