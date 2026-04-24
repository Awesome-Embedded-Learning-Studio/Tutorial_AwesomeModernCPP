// 验证结构化绑定在 constexpr 函数中的使用
// 修正文档中的错误断言：不能在命名空间作用域使用 constexpr 结构化绑定

#include <utility>
#include <cassert>

constexpr auto get_point() {
    return std::make_pair(3, 4);
}

// 下面的写法是错误的，结构化绑定不能在命名空间作用域声明为 constexpr
// constexpr auto [x, y] = get_point();  // 编译错误
// static_assert(x == 3 && y == 4);

// 正确做法：在 constexpr 函数内部使用
constexpr bool test_structured_binding() {
    auto [x, y] = get_point();
    return x == 3 && y == 4;
}

int main() {
    constexpr auto result = test_structured_binding();
    static_assert(result, "test failed");

    // 注意：不能在 if constexpr 的条件中使用结构化绑定
    // if constexpr (auto [x, y] = get_point(); x == 3) { ... }  // 编译错误
    // 因为结构化绑定变量不是常量表达式

    return 0;
}
