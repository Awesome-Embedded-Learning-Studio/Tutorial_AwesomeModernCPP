// requires 表达式不求值(unevaluated):里面的调用只检查能否编译,根本不执行、无副作用
// 对应文章:documents/vol4-advanced/vol3-metaprogramming-cpp20-23/03-requires-expressions.md
// 编译运行:g++ -Wall -Wextra -std=c++20 unevaluated.cpp -o ue && ./ue
// 预期:concept 求值时 counter 仍为 0,只有 main 里真正调用 increment 才变 1
#include <iostream>

int counter = 0;
int increment() {
    ++counter;
    std::cout << "[副作用] increment 被调用了\n";
    return 1;
}

template <typename T>
concept MentionsIncrement = requires(T t) {
    increment(); // 只检查「这个调用合不合法」,不求值、不执行
};

int main() {
    static_assert(MentionsIncrement<int>); // 满足:increment() 调用合法
    std::cout << "concept 求值完毕,counter = " << counter << "\n";
    increment(); // 这里才真正调用
    std::cout << "真正调用后,counter = " << counter << "\n";
    return 0;
}
