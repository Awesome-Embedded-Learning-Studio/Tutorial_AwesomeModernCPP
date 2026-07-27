// 配套 04-tmp-core-techniques.md「void_t 与 detection idiom」
// 用 void_t 的 detection idiom 检测类型是否有 value_type 内嵌类型
// 编译运行:g++ -std=c++20 -Wall -Wextra void_t_detection.cpp -o vtd && ./vtd
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

// detection idiom:用 void_t 检测 T 是否有 value_type 内嵌类型
// 主模板:默认继承 false_type(给个 void 占位,匹配不上特化时落到这里)
template <typename T, typename = void> struct has_value_type : std::false_type {};

// 偏特化:只有当 void_t<...> 里的替换成功时,这个特化才「更特化」从而被选中
template <typename T> struct has_value_type<T, std::void_t<typename T::value_type>>
    : std::true_type {};

template <typename T> constexpr bool has_value_type_v = has_value_type<T>::value;

int main() {
    std::cout << std::boolalpha;
    std::cout << "has_value_type_v<std::vector<int>>: "
              << has_value_type_v<std::vector<int>> << "\n";
    std::cout << "has_value_type_v<std::string>:      " << has_value_type_v<std::string> << "\n";
    std::cout << "has_value_type_v<int>:              " << has_value_type_v<int> << "\n";
}
