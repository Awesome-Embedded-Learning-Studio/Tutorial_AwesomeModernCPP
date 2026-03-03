// variant_basics.cpp
// std::variant 基本用法示例

#include <variant>
#include <string>
#include <iostream>

int main() {
    std::cout << "=== std::variant Basics ===\n\n";

    // 创建 variant，默认持有第一个类型（int，值为 0）
    std::variant<int, std::string> v;
    std::cout << "Default variant holds int: " << std::get<int>(v) << "\n";

    // 赋值为 int
    v = 42;
    std::cout << "Assigned int: " << std::get<int>(v) << "\n";

    // 赋值为 string
    v = std::string("hello variant");
    std::cout << "Assigned string: " << std::get<std::string>(v) << "\n";

    // 安全访问：检查类型
    if (std::holds_alternative<std::string>(v)) {
        std::cout << "Currently holds string: " << std::get<std::string>(v) << "\n";
    }

    // 使用 std::visit 访问
    std::cout << "\n=== std::visit ===\n";
    std::visit([](auto &&x) {
        std::cout << "visit: " << x << "\n";
    }, v);

    // index() 返回当前类型的索引
    std::cout << "Current index: " << v.index() << "\n";

    return 0;
}
