// 配套 04-ctad.md「少写一摞尖括号:CTAD 基本用法」
// 演示 pair/tuple/vector/lock_guard 用 CTAD 省掉尖括号,用 static_assert 验证推导结果
// 编译运行:g++ -std=c++17 -Wall -Wextra basic_ctad.cpp -o basic_ctad && ./basic_ctad
#include <iostream>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

int main() {
    // pair:两个参数类型不同,推 pair<int,double>
    std::pair p(1, 2.5);
    static_assert(std::is_same_v<decltype(p), std::pair<int, double>>);

    // pair:两个都是 int 字面量,推 pair<int,int>
    std::pair p2(1, 2);
    static_assert(std::is_same_v<decltype(p2), std::pair<int, int>>);

    // tuple:任意个参数各推各的
    std::tuple t(1, 2.5, "hi");
    static_assert(std::is_same_v<decltype(t), std::tuple<int, double, const char*>>);

    // vector:initializer_list 推元素类型
    std::vector v{1, 2, 3};
    static_assert(std::is_same_v<decltype(v), std::vector<int>>);

    // lock_guard:从互斥量推 lock_guard<std::mutex>
    std::mutex m;
    std::lock_guard lk(m);
    static_assert(std::is_same_v<decltype(lk), std::lock_guard<std::mutex>>);

    std::cout << "p   = (" << p.first << ", " << p.second << ")\n";
    std::cout << "t   = (" << std::get<0>(t) << ", " << std::get<1>(t) << ", " << std::get<2>(t)
              << ")\n";
    std::cout << "v   = {" << v[0] << ", " << v[1] << ", " << v[2] << "}\n";
    std::cout << "所有 static_assert 通过,CTAD 推导结果与手写尖括号一致\n";
    return 0;
}
