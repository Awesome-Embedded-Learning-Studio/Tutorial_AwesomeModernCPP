// Standard: C++20
// std::source_location::current() 是 constexpr，编译期就把 file/line/func/column「焊」进常量。
// static_assert 证明 line_of() 编译期可求值；看汇编(allow-x86-asm)会发现 current 一次都没被调用——
// 传 source_location 的开销和传几个 int/string_view 一样，零运行时调用。
#include <iostream>
#include <source_location>

constexpr int line_of(std::source_location loc = std::source_location::current()) {
    return loc.line();
}

// static_assert 证明: current() 的结果在编译期就是确定的
static_assert(line_of() == __LINE__, "line_of must be usable in constant expression");

#define LEGACY_LOG() \
    std::cout << "[legacy] " << __FILE__ << ":" << __LINE__ << " (no func, no col)\n"

int main() {
    constexpr int here = line_of();
    std::cout << "constexpr line_of() = " << here << '\n';
    LEGACY_LOG();
}
