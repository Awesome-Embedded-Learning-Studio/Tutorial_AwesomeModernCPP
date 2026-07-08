// static_assert 把关:NoDestructor 拒绝平凡析构 T,引导用 constinit/裸静态
// 来源:NoDestructor 实战(二)(三)
// 编译:g++ -std=c++20 -Wall -Wextra -I. 25_static_assert_gatekeep.cpp -o 25_static_assert_gatekeep
// 取消下面注释的"误用"行,会触发 static_assert 编译失败(这是预期的把关)

#include "no_destructor.hpp"

#include <cstdint>
#include <iostream>
#include <string>

int main() {
    std::cout << "=== static_assert 把关演示 ===\n\n";

    std::cout << "[1] 正确用法:非平凡析构 T(std::string)\n";
    static const tamcpp::chrome::NoDestructor<std::string> s("ok");
    std::cout << "  NoDestructor<string> 构造成功:*s = " << *s << "\n\n";

    std::cout << "[2] 误用 1:平凡可构造+析构 T(int)——static_assert 拒绝\n";
    std::cout << "  若取消下面这行的注释,编译期报错(static_assert):\n";
    std::cout
        << "    error: T is trivially constructible and destructible; please use constinit T\n";
    std::cout << "  (int 同时平凡可构造+析构,触发第一条更具体的断言,而非第二条)\n";
    // tamcpp::chrome::NoDestructor<int> bad_int(42);   // ← 取消注释触发 static_assert

    std::cout << "\n[3] 误用 2:平凡可构造+析构(uint64_t)——static_assert 拒绝\n";
    std::cout << "  若取消下面这行,编译期报错:\n";
    std::cout << "    error: T is trivially constructible and destructible; use constinit T\n";
    // tamcpp::chrome::NoDestructor<uint64_t> bad_seed(42);   // ← 取消注释触发

    std::cout << "\n[4] 正确替代:平凡析构 T 直接用裸静态(无需 NoDestructor)\n";
    static const uint64_t kSeed = 42; // ✓ 平凡析构,不产生全局析构器
    std::cout << "  static const uint64_t kSeed = " << kSeed << "(裸静态,无需 NoDestructor)\n";

    std::cout << "\n[5] 正确替代:常量可构造用 constinit(C++20)\n";
    static constexpr int kMax = 100; // ✓ 编译期常量初始化,零运行时代码
    std::cout << "  static constexpr int kMax = " << kMax << "(编译期,无 ctor/dtor)\n";

    std::cout << "\n小结:NoDestructor 只服务非平凡析构 T;static_assert "
                 "在编译期拦住误用并给出正确替代。\n";
    return 0;
}
