// Concepts 的三个经典坑,用宏切换复现(默认编译干净,加宏触发对应坑的编译失败)
// 对应文章:02-constraining-templates.md、03-requires-expressions.md
//
// 默认编译(干净,main 演示「用 concept 包装优雅判断负例」的解法):
//   g++ -Wall -Wextra -std=c++20 concept_pitfalls.cpp -o cp && ./cp
// 复现三个坑(每个都会编译失败,对照文章看报错):
//   g++ -std=c++20 -DDEMO_AMBIGUITY  concept_pitfalls.cpp   # 坑一:两个互不蕴含的 concept 重载,Duck
//   同时满足 -> 歧义 g++ -std=c++20 -DDEMO_NOSUBSUME  concept_pitfalls.cpp   # 坑二:C2=C1<T>
//   规范化后原子约束与 C1 相同,不 subsume -> 歧义 g++ -std=c++20 -DDEMO_HARD_ERROR
//   concept_pitfalls.cpp   # 坑三:对具体类型直接写 requires 表达式 -> 硬错误
#include <iostream>
#include <string>

// 坑一用:两个彼此独立的 concept,谁也不蕴含谁
template <typename T>
concept Swimmable = requires(T t) { t.swim(); };
template <typename T>
concept Flyable = requires(T t) { t.fly(); };
void act(Swimmable auto) {}
void act(Flyable auto) {}

// 坑二用:C2 只是 C1<T> 换名,没有额外原子约束
template <typename T>
concept C1 = requires(T t) { t.a(); };
template <typename T>
concept C2 = C1<T>;
void g(C1 auto) {}
void g(C2 auto) {}

// 坑三的解法:把 requires 表达式包进 concept,求值时 T 是模板参数 -> SFINAE 友好,失败返回 false
// 而非硬错误
template <typename T>
concept HasNope = requires(T t) { t.nope(); };

struct Duck {
    void swim() {}
    void fly() {}
};
struct X {
    void a() {}
};

int main() {
    std::cout << std::boolalpha;
    // 解法演示:concept 包装后,不存在的成员优雅返回 false
    std::cout << "HasNope<std::string>: " << HasNope<std::string> << "\n"; // false,不硬错误

#if DEMO_AMBIGUITY
    act(Duck{}); // Duck 同时满足 Swimmable 和 Flyable,两者互不蕴含 -> 编译器选不出 -> 歧义
#elif DEMO_NOSUBSUME
    g(X{}); // X 同时满足 C1 和 C2,但 C2 规范化后原子约束= C1,无真包含 -> 歧义
#elif DEMO_HARD_ERROR
    static_assert(!requires(std::string s) { s.nope(); }); // 对具体类型 string 直接写 -> 硬错误
#endif
    return 0;
}
