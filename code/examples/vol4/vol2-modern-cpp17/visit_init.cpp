// 配套 01-if-constexpr.md「init-statement 与 std::visit 泛型 lambda」
// 两个实战点合在一起:if constexpr (init; cond) 形式 + 泛型 lambda 里 if constexpr 替代一摞重载
// 编译运行:g++ -std=c++17 -Wall -Wextra visit_init.cpp -o vi && ./vi
#include <iostream>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

template <typename T> void first_kind(const T& container) {
    // init-statement:先取首元素 v,再在条件里判类型,v 在两个分支里都可见
    if constexpr (auto v = *container.begin(); std::is_integral_v<decltype(v)>) {
        std::cout << "装的是整数,第一个:" << v << "\n";
    } else {
        std::cout << "装的不是整数\n";
    }
}

// 泛型 lambda + if constexpr:一把替代三个 operator() 重载
auto print_variant = [](const auto& v) {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, int>) {
        std::cout << "int:" << v << "\n";
    } else if constexpr (std::is_same_v<T, double>) {
        std::cout << "double:" << v << "\n";
    } else {
        std::cout << "string:" << v << "\n";
    }
};

int main() {
    std::vector<int> vi{10, 20};
    std::vector<double> vd{1.5, 2.5};
    first_kind(vi);
    first_kind(vd);

    std::variant<int, double, std::string> var = 42;
    std::visit(print_variant, var);
    var = "hello";
    std::visit(print_variant, var);
}
