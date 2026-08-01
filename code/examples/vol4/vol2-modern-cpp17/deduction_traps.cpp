// 配套 04-ctad.md「圆括号与花括号的语义差,以及最窄可行类型」
// 默认演示能编过的对照案例;加 -DNARROW_FAIL 复现:initializer_list 推不出公共类型时报错
// 编译运行:g++ -std=c++17 -Wall -Wextra deduction_traps.cpp -o deduction_traps && ./deduction_traps
// 复现报错:  g++ -std=c++17 -Wall -Wextra -DNARROW_FAIL deduction_traps.cpp -o deduction_traps
#include <iostream>
#include <type_traits>
#include <vector>

int main() {
    // 陷阱一:圆括号 vs 花括号语义不同
    std::vector a(10, 0); // (count, value):10 个 0
    std::vector b{10, 0}; // {initializer_list}:两个元素 10 和 0
    std::cout << "a (10,0): size=" << a.size() << "  [0]=" << a[0] << "  [9]=" << a[9] << "\n";
    std::cout << "b {10,0}: size=" << b.size() << "  [0]=" << b[0] << "  [1]=" << b[1] << "\n";

    // 陷阱二:复制构造推导 vs initializer_list 推导
    std::vector<int> src{1, 2, 3};
    std::vector cpy(src); // 复制构造推导:vector<int>
    static_assert(std::is_same_v<decltype(cpy), std::vector<int>>);
    std::cout << "cpy: size=" << cpy.size() << "  [2]=" << cpy[2] << "\n";

    // 陷阱三:initializer_list 元素取公共类型(不窄化)
    std::vector same_int{1, 2, 3}; // 全 int -> vector<int>
    std::vector mix{1, 2.5};       // int+double -> vector<double>(公共类型)
    static_assert(std::is_same_v<decltype(same_int), std::vector<int>>);
    static_assert(std::is_same_v<decltype(mix), std::vector<double>>);
    std::cout << "mix {1, 2.5}: [0]=" << mix[0] << "  [1]=" << mix[1]
              << "  (类型是 vector<double>)\n";

#ifdef NARROW_FAIL
    // int 与 long long 没有非窄化的公共 initializer_list 元素类型,推不出
    std::vector bad{1, 2, 3, 100000000000LL}; // 编不过
    (void)bad;
#endif
    return 0;
}
