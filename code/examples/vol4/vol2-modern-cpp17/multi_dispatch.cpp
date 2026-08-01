// 配套 01-if-constexpr.md「else if constexpr 多分派:把一摞重载拍扁」
// 演示丢弃分支里依赖 T 的操作不报错:传 int 时 string 分支不存在,传 string 时 else 分支不存在
// 编译运行:g++ -std=c++17 -Wall -Wextra multi_dispatch.cpp -o md && ./md
#include <iostream>
#include <string>
#include <type_traits>

template <typename T> void inspect(T& x) {
    if constexpr (std::is_integral_v<T>) {
        std::cout << "整数:" << x << ",翻倍:" << (x + x) << "\n";
    } else if constexpr (std::is_same_v<T, std::string>) {
        std::cout << "字符串长度:" << x.size() << "\n"; // 只有 string 才有 size
        x.append("!");
    } else {
        x.foo(); // 只有走 else 分支的类型才需要 foo 这个成员
    }
}

struct HasFoo {
    void foo() {}
    int v;
};

int main() {
    int n = 7;
    inspect(n); // 走 integral,else 里的 x.foo() 不实例化,int 没 foo 也不报错
    std::string s = "hi";
    inspect(s); // 走 string 分支
    HasFoo h{};
    inspect(h); // 走 else 分支,调用 h.foo()
    std::cout << "s 现在是:" << s << "\n";
}
