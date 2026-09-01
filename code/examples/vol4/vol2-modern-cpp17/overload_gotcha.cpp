// 配套 03-perfect-forwarding.md「转发引用 + 重载的灾难」
// 演示转发引用 T&& 在重载决议里贪婪地抢走左值实参,击败看似更特化的 const& 重载
// 这是反直觉但能编的例子,默认编译运行即可复现
// 编译运行:g++ -std=c++17 -Wall -Wextra overload_gotcha.cpp -o og && ./og
#include <iostream>
using namespace std;

// 函数签名宏:GCC/Clang 是 __PRETTY_FUNCTION__,MSVC 是 __FUNCSIG__
#ifdef _MSC_VER
#define PRETTY_FUNCTION __FUNCSIG__
#else
#define PRETTY_FUNCTION __PRETTY_FUNCTION__
#endif

struct Widget {
    int v;
};

// 三组重载,本意:const& 接左值,&& 接右值,T&& 只兜底别的类型
void tag(const Widget&) {
    cout << "  命中 const Widget& 重载\n";
}
void tag(Widget&&) {
    cout << "  命中 Widget&& 重载\n";
}
template <typename T> void tag(T&&) {
    // 转发引用:T&& 推导后也是精确匹配,模板还更优,反而抢了主角
    cout << "  命中 T&& 转发引用:" << PRETTY_FUNCTION << "\n";
}

int main() {
    Widget w{1};
    cout << "tag(w)        传左值(直觉该走 const Widget&):\n";
    tag(w); // 实际:T&& 推成 Widget&,精确匹配 + 模板,把 const& 挤掉了
    cout << "tag(Widget{2}) 传右值:\n";
    tag(Widget{2}); // 右值:非模板的 Widget&& 精确匹配,优先于模板

    // 教训:别让转发引用和你想特化的重载同时出现。
    // 隔开的办法:用 const T& 接左值、用 tag_impl 之类的命名函数转发、或用 concepts 约束 T&&。
}
