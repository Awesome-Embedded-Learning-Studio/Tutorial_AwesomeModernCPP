// 配套 08-templates-and-exception-safety.md「move_if_noexcept:可能要回滚时,退回拷贝」
// 演示 move_if_noexcept 怎么按 move 是否 noexcept 在 move/copy 之间挑
// 编译运行:g++ -std=c++20 -Wall -Wextra move_if_noexcept_demo.cpp -o mind && ./mind
#include <iostream>
#include <type_traits>
#include <utility>

// 移动构造 noexcept:move_if_noexcept 会选 move
struct NothrowMove {
    int* p;
    explicit NothrowMove(int v) : p(new int(v)) {}
    ~NothrowMove() { delete p; }
    NothrowMove(const NothrowMove& o) : p(new int(*o.p)) {
        std::cout << "    [NothrowMove] 被拷贝\n";
    }
    NothrowMove(NothrowMove&& o) noexcept : p(o.p) {
        o.p = nullptr;
        std::cout << "    [NothrowMove] 被移动\n";
    }
};

// 移动构造可能抛:move_if_noexcept 会退回选 copy
struct ThrowingMove {
    int* p;
    explicit ThrowingMove(int v) : p(new int(v)) {}
    ~ThrowingMove() { delete p; }
    ThrowingMove(const ThrowingMove& o) : p(new int(*o.p)) {
        std::cout << "    [ThrowingMove] 被拷贝\n";
    }
    ThrowingMove(ThrowingMove&& o) noexcept(false) : p(o.p) { // 故意 noexcept(false)
        o.p = nullptr;
        std::cout << "    [ThrowingMove] 被移动\n";
    }
};

int main() {
    std::cout << std::boolalpha;
    std::cout << "is_nothrow_move_constructible:\n";
    std::cout << "  NothrowMove:  " << std::is_nothrow_move_constructible_v<NothrowMove> << "\n";
    std::cout << "  ThrowingMove: " << std::is_nothrow_move_constructible_v<ThrowingMove> << "\n";

    std::cout << "move_if_noexcept 对 NothrowMove(应为 move):\n";
    NothrowMove nm(1);
    [[maybe_unused]] auto nm2 = std::move_if_noexcept(nm);

    std::cout << "move_if_noexcept 对 ThrowingMove(应为 copy):\n";
    ThrowingMove tm(2);
    [[maybe_unused]] auto tm2 = std::move_if_noexcept(tm);
}
