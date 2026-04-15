// variant_visit.cpp
// std::visit 与重载集合示例

#include <variant>
#include <iostream>
#include <string>

// 重载集合技巧
template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

int main() {
    std::cout << "=== std::visit with Overloaded ===\n\n";

    std::variant<int, double, std::string> v = 3.14;

    std::cout << "Holding double:\n";
    std::visit(overloaded{
        [](int i) { std::cout << "  int: " << i << "\n"; },
        [](double d) { std::cout << "  double: " << d << "\n"; },
        [](const std::string &s) { std::cout << "  string: " << s << "\n"; }
    }, v);

    v = 42;
    std::cout << "\nHolding int:\n";
    std::visit(overloaded{
        [](int i) { std::cout << "  int: " << i << "\n"; },
        [](double d) { std::cout << "  double: " << d << "\n"; },
        [](const std::string &s) { std::cout << "  string: " << s << "\n"; }
    }, v);

    v = std::string("hello");
    std::cout << "\nHolding string:\n";
    std::visit(overloaded{
        [](int i) { std::cout << "  int: " << i << "\n"; },
        [](double d) { std::cout << "  double: " << d << "\n"; },
        [](const std::string &s) { std::cout << "  string: " << s << "\n"; }
    }, v);

    return 0;
}
