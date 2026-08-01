// 配套 03-perfect-forwarding.md「std::forward 在做什么:恢复值类别」
// wrap 用 std::forward<T>(x) 把实参原样转发,target 的左值/右值重载都能正确走到
// 编译运行:g++ -std=c++17 -Wall -Wextra wrap_with_forward.cpp -o wwf && ./wwf
#include <iostream>
#include <string>
#include <utility>
using namespace std;

void target(string& s) {
    cout << "  [target] 命中左值重载:" << s << "\n";
}
void target(string&& s) {
    cout << "  [target] 命中右值重载:" << s << "\n";
}

// 透明转发器:把实参原样传给 target,保持值类别
template <typename T> void wrap(T&& x) {
    target(std::forward<T>(x));
}

int main() {
    string s = "hello";
    cout << "wrap(s)                传左值:\n";
    wrap(s);
    cout << "wrap(string(\"world\")) 传右值:\n";
    wrap(string("world"));
    cout << "wrap(std::move(s))     传右值:\n";
    wrap(std::move(s));
}
