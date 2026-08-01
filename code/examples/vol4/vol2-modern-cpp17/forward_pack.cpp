// 配套 02-variadic-templates.md「完美转发的包」
// std::forward<Args>(args)... 把任意个实参的值类别原样转发,这是 make_unique 风格工厂的核心
// 编译运行:g++ -std=c++17 -Wall -Wextra forward_pack.cpp -o fp && ./fp
#include <iostream>
#include <memory>
#include <string>
#include <utility>

// 追踪值类别的小工具类
struct Tracked {
    Tracked() { std::cout << "  Tracked() 默认构造\n"; }
    Tracked(const Tracked&) { std::cout << "  Tracked(const&) 拷贝\n"; }
    Tracked(Tracked&&) noexcept { std::cout << "  Tracked(Tracked&&) 移动\n"; }
};

// 接收端:两个重载,一个吃 lvalue ref,一个吃 rvalue ref
void sink(const Tracked&) {
    std::cout << "  -> sink(const Tracked&) 收到 lvalue\n";
}
void sink(Tracked&&) {
    std::cout << "  -> sink(Tracked&&)      收到 rvalue\n";
}

// 中转函数:接收任意个 forwarding reference,原样转发
// forward<Args>(args)... 是模式展开:展开成 forward<A0>(a0), forward<A1>(a1), ...
// 每个实参按它自己的 Args 独立 forward,值类别不丢
template <typename... Args> void relay(Args&&... args) {
    sink(std::forward<Args>(args)...);
}

// make_unique 风格的工厂:把任意实参完美转发给 T 的构造函数
template <typename T, typename... Args> std::unique_ptr<T> make_tracked(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

int main() {
    std::cout << "传一个具名对象(lvalue):\n";
    Tracked t;
    relay(t); // t 是 lvalue,forward 成 lvalue,sink 走 const& 重载

    std::cout << "\n传一个临时对象(rvalue):\n";
    relay(Tracked{}); // 临时对象是 rvalue,forward 成 rvalue,sink 走 && 重载

    std::cout << "\n工厂转发给 string 构造函数(\"hello\", 2):取前 2 个字符\n";
    auto p = make_tracked<std::string>("hello", 2);
    std::cout << "  *p = \"" << *p << "\"\n";
}
