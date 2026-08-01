// 配套 03-perfect-forwarding.md「转发引用与右值引用不是一回事」
// 演示 T&& 在模板里同时绑左值和右值,T 的推导结果随实参值类别变化
// 编译运行:g++ -std=c++17 -Wall -Wextra forwarding_reference_deduce.cpp -o frd && ./frd
#include <iostream>
#include <type_traits>
using namespace std;

template <typename T> void show(T&& x) {
    // 注意:这里看的是模板参数 T 本身,以及 x 实例化后的真实类型
    cout << "  T 是否左值引用:" << is_lvalue_reference_v<T> << "  T 是否右值引用:"
         << is_rvalue_reference_v<T> << "\n";
    using X = decltype(x);
    cout << "  x 是否左值引用:" << is_lvalue_reference_v<X> << "  x 是否右值引用:"
         << is_rvalue_reference_v<X> << "\n";
}

int main() {
    int a = 10;
    cout << "传左值 a:\n";
    show(a); // T 推成 int&,x 类型 int&
    cout << "传右值 20:\n";
    show(20); // T 推成 int, x 类型 int&&
    cout << "传 std::move(a):\n";
    show(std::move(a)); // 同上,x 类型 int&&
}
