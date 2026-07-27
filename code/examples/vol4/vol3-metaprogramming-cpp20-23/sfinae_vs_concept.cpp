// 配套 04-tmp-core-techniques.md「SFINAE 往 concepts 迁移」
// 同一需求(只接受整数)的两种写法:enable_if 老办法 vs concept 新办法
//
// 默认编译跑:两种写法对 int 都正常
//   g++ -std=c++20 -Wall -Wextra sfinae_vs_concept.cpp -o svc && ./svc
//
// 看 SFINAE 版报错(用 string 触发,对照正文里 enable_if 的天书):
//   g++ -std=c++20 -Wall -Wextra -DSHOW_SFINAE_ERROR sfinae_vs_concept.cpp
//
// 看 concept 版报错(对照正文里 constraints not satisfied 的人话):
//   g++ -std=c++20 -Wall -Wextra -DSHOW_CONCEPT_ERROR sfinae_vs_concept.cpp
#include <concepts>
#include <iostream>
#include <string>
#include <type_traits>

// SFINAE 老办法:约束藏在默认模板参数里
template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>> T add_old(T a, T b) {
    return a + b;
}

// concept 新办法:约束写在签名里
template <typename T>
    requires std::integral<T>
T add_new(T a, T b) {
    return a + b;
}

int main() {
#ifdef SHOW_SFINAE_ERROR
    add_old(std::string("a"), std::string("b"));
#elif defined(SHOW_CONCEPT_ERROR)
    add_new(std::string("a"), std::string("b"));
#else
    std::cout << "add_old(2, 3) = " << add_old(2, 3) << "\n";
    std::cout << "add_new(2, 3) = " << add_new(2, 3) << "\n";
    std::cout << "两种写法对 int 都正常;想看报错加 -DSHOW_SFINAE_ERROR 或 -DSHOW_CONCEPT_ERROR\n";
#endif
}
