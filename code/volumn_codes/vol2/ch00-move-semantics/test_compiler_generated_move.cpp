// test_compiler_generated_move.cpp -- 验证编译器生成的移动操作行为
// Standard: C++17
// GCC 15, -O2 -std=c++17

#include <iostream>
#include <string>
#include <vector>
#include <utility>

struct MemberTracker
{
    std::string name;
    int value;

    MemberTracker(const char* n, int v) : name(n), value(v) {
        std::cout << "  [" << name << "] 构造\n";
    }

    MemberTracker(const MemberTracker& other)
        : name(other.name + "_copy"), value(other.value) {
        std::cout << "  [" << name << "] 拷贝构造\n";
    }

    MemberTracker(MemberTracker&& other) noexcept
        : name(std::move(other.name)), value(other.value) {
        other.name = "(moved)";
        std::cout << "  [" << name << "] 移动构造\n";
    }

    MemberTracker& operator=(const MemberTracker&) = default;
    MemberTracker& operator=(MemberTracker&&) noexcept = default;
};

class Aggregate
{
public:
    MemberTracker m1;
    MemberTracker m2;
    int scalar;

    // 使用编译器自动生成的移动构造函数
    Aggregate(const char* n1, const char* n2, int s)
        : m1(n1, 1), m2(n2, 2), scalar(s) {
        std::cout << "  [Aggregate] 构造完成\n";
    }

    // 编译器生成的移动构造会逐个移动成员
    ~Aggregate() = default;
    Aggregate(const Aggregate&) = default;
    Aggregate(Aggregate&&) noexcept = default;
    Aggregate& operator=(const Aggregate&) = default;
    Aggregate& operator=(Aggregate&&) noexcept = default;
};

int main()
{
    std::cout << "=== 测试编译器生成的移动构造 ===\n\n";

    std::cout << "创建源对象:\n";
    Aggregate src("Alpha", "Beta", 42);

    std::cout << "\n移动构造目标对象:\n";
    Aggregate dest = std::move(src);

    std::cout << "\n移动后的状态:\n";
    std::cout << "  src.m1.name: " << src.m1.name << "\n";
    std::cout << "  src.m2.name: " << src.m2.name << "\n";
    std::cout << "  src.scalar: " << src.scalar << "\n";
    std::cout << "  dest.m1.name: " << dest.m1.name << "\n";
    std::cout << "  dest.m2.name: " << dest.m2.name << "\n";
    std::cout << "  dest.scalar: " << dest.scalar << "\n";

    std::cout << "\n=== 结论 ===\n";
    std::cout << "编译器生成的移动构造函数确实会逐个调用成员的移动构造函数。\n";
    std::cout << "每个成员（m1, m2）都被移动，标量成员（scalar）被复制。\n";
    std::cout << "这符合 C++ 标准的规定（参见 [class.copy.ctor]）。\n";

    return 0;
}
