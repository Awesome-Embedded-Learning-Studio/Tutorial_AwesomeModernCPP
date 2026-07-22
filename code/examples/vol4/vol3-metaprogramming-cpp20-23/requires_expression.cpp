// Requires 表达式四种成分(简单/类型/复合/嵌套)+ 当 bool 用 + 用表达式定义 concept
// 对应文章:documents/vol4-advanced/vol3-metaprogramming-cpp20-23/03-requires-expressions.md
// 编译运行:g++ -Wall -Wextra -std=c++20 requires_expression.cpp -o req && ./req
#include <concepts>
#include <iostream>
#include <vector>

// 四种成分一次用全:简单要求、类型要求、复合要求、嵌套要求
template <typename T>
concept Container = requires(T t) {
    t.begin(); // ① 简单要求
    t.end();
    typename T::value_type;                           // ② 类型要求
    { t.size() } -> std::convertible_to<std::size_t>; // ③ 复合要求
    requires std::integral<typename T::value_type>;   // ④ 嵌套要求
};

static_assert(Container<std::vector<int>>); // value_type=int,integral 通过
static_assert(!Container<int>);             // int 没有 begin/end

// requires 表达式当 bool 用:if constexpr 里直接判断
template <typename T> void process(T t) {
    (void)t;
    if constexpr (requires(T x) { x.empty(); }) {
        std::cout << "has empty()\n";
    } else {
        std::cout << "no empty()\n";
    }
}

int main() {
    process(std::vector<int>{}); // vector 有 empty()
    process(42);                 // int 没有
    return 0;
}
