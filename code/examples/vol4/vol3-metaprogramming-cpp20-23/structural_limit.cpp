// 配套 05-compile-time-strings.md「structural 的限制」
// NTTP class type 要求类型是 structural:所有基类和非静态数据成员必须 public
//
// 默认编译跑:Open(public 数据成员)能作 NTTP
//   g++ -std=c++20 -Wall -Wextra structural_limit.cpp -o sl && ./sl
// 看 Secret 不能作 NTTP 的报错:加 -DSHOW_NON_STRUCTURAL
//   g++ -std=c++20 -Wall -Wextra -DSHOW_NON_STRUCTURAL structural_limit.cpp
#include <iostream>

// ✅ structural:public 数据成员
struct Open {
    int x;
    int y;
};

template <Open O> struct Wrapper {
    static constexpr int sum = O.x + O.y;
};

#ifdef SHOW_NON_STRUCTURAL
// ❌ 非 structural:默认 class 的成员是 private
class Secret {
    int x; // private
  public:
    constexpr Secret(int v) : x(v) {}
};

template <Secret S> struct Bad {}; // 报错:Secret 不是 structural type
Bad<Secret{1}> bad;
#else
int main() {
    static_assert(Wrapper<Open{3, 4}>{}.sum == 7);
    static_assert(Wrapper<Open{10, 20}>{}.sum == 30);
    std::cout << "Open{3,4} 的 sum = " << Wrapper<Open{3, 4}>{}.sum << "\n";
    std::cout << "public 成员的 struct 能作 NTTP;private 成员的不行,加 -DSHOW_NON_STRUCTURAL 看\n";
}
#endif
