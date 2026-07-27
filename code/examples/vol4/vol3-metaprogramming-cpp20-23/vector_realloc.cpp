// 配套 08-templates-and-exception-safety.md「vector 扩容凭什么保住强异常保证」
// vector 扩容时,move noexcept 的元素被 move,move 可能抛的元素被 copy(保强保证)
// 编译运行:g++ -std=c++20 -Wall -Wextra vector_realloc.cpp -o vr && ./vr
#include <iostream>
#include <vector>

// 故意写得能追踪 copy/move 次数
struct NothrowMove {
    int v;
    explicit NothrowMove(int x) : v(x) {}
    NothrowMove(const NothrowMove& o) : v(o.v) { std::cout << "    copy\n"; }
    NothrowMove(NothrowMove&& o) noexcept : v(o.v) { std::cout << "    move\n"; }
};

struct ThrowingMove {
    int v;
    explicit ThrowingMove(int x) : v(x) {}
    ThrowingMove(const ThrowingMove& o) : v(o.v) { std::cout << "    copy\n"; }
    ThrowingMove(ThrowingMove&& o) noexcept(false) : v(o.v) { std::cout << "    move\n"; }
};

int main() {
    std::cout << "vector<NothrowMove> 预留 2,再 push 第三个触发扩容:\n";
    {
        std::vector<NothrowMove> v;
        v.reserve(2);
        v.emplace_back(1);
        v.emplace_back(2);
        std::cout << "  >>> 扩容时(move noexcept,应为 move):\n";
        v.emplace_back(3);
    }

    std::cout << "\nvector<ThrowingMove> 预留 2,再 push 第三个触发扩容:\n";
    {
        std::vector<ThrowingMove> v;
        v.reserve(2);
        v.emplace_back(1);
        v.emplace_back(2);
        std::cout << "  >>> 扩容时(move 可能抛,应为 copy 保强异常保证):\n";
        v.emplace_back(3);
    }
}
