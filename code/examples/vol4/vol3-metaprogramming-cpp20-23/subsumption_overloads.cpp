// Subsumption(约束蕴含):多个带 concept 约束的重载,编译器靠约束的包含关系选最特定的
// 对应文章:documents/vol4-advanced/vol3-metaprogramming-cpp20-23/02-constraining-templates.md
// 编译运行:g++ -Wall -Wextra -std=c++20 subsumption_overloads.cpp -o sub && ./sub
#include <iostream>

// 第一组:Dog 蕴含 Animal(Dog = Animal<T> && 额外要求),subsumption 让更窄的重载胜出
template <typename T>
concept Animal = requires(T t) { t.eat(); };
template <typename T>
concept Dog = Animal<T> && requires(T t) { t.bark(); };

void describe(Animal auto const&) {
    std::cout << "an animal\n";
}
void describe(Dog auto const&) {
    std::cout << "a dog\n";
}

// 第二组:&& 组合产生 {A<T>, B<T>} 原子约束集合,C 同时 subsumes A 和 B
template <typename T>
concept A = requires(T t) { t.a(); };
template <typename T>
concept B = requires(T t) { t.b(); };
template <typename T>
concept C = A<T> && B<T>;

void f(A auto const&) {
    std::cout << "A\n";
}
void f(B auto const&) {
    std::cout << "B\n";
}
void f(C auto const&) {
    std::cout << "C\n";
}

struct Cat {
    void eat() {}
};
struct Pup {
    void eat() {}
    void bark() {}
};
struct Both {
    void a() {}
    void b() {}
};

int main() {
    describe(Cat{}); // 只满足 Animal -> 宽重载
    describe(Pup{}); // 满足 Dog -> 窄重载(subsumes 宽重载)
    f(Both{});       // 同时满足 A、B、C -> C(subsumes A 和 B)
    return 0;
}
