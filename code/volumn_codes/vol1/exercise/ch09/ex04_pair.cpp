/**
 * @file ex04_pair.cpp
 * @brief 练习：类模板 — 实现泛型 Pair<T, U>
 *
 * 实现一个存储两个不同类型值的 Pair 类模板，提供 first()/second()
 * 访问器（const 和非 const 版本）以及 swap() 成员函数。
 * 分别用 Pair<int, std::string> 和 Pair<double, char> 测试。
 */

#include <iostream>
#include <string>
#include <utility>

/**
 * @brief 泛型二元组，存储两个不同类型的值
 *
 * @tparam T 第一个元素的类型
 * @tparam U 第二个元素的类型
 */
template <typename T, typename U>
class Pair
{
public:
    /// @brief 默认构造，值初始化
    Pair() : first_(), second_() {}

    /// @brief 用两个值构造
    Pair(const T& first, const U& second)
        : first_(first), second_(second) {}

    /// @brief 访问第一个元素（可修改）
    T& first() { return first_; }

    /// @brief 访问第一个元素（只读）
    const T& first() const { return first_; }

    /// @brief 访问第二个元素（可修改）
    U& second() { return second_; }

    /// @brief 访问第二个元素（只读）
    const U& second() const { return second_; }

    /// @brief 交换当前 Pair 与另一个 Pair 的内容
    void swap(Pair& other)
    {
        using std::swap;
        swap(first_, other.first_);
        swap(second_, other.second_);
    }

private:
    T first_;
    U second_;
};

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== ex04: Pair<T, U> 类模板 =====\n\n";

    // --- Pair<int, std::string> ---
    {
        Pair<int, std::string> p1(1, std::string("hello"));
        Pair<int, std::string> p2(2, std::string("world"));

        std::cout << "Pair<int, std::string> 测试:\n";
        std::cout << "  p1: first=" << p1.first()
                  << ", second=\"" << p1.second() << "\"\n";
        std::cout << "  p2: first=" << p2.first()
                  << ", second=\"" << p2.second() << "\"\n";

        // 测试非 const 访问器修改
        p1.first() = 10;
        p1.second() = "modified";
        std::cout << "  修改后 p1: first=" << p1.first()
                  << ", second=\"" << p1.second() << "\"\n";

        // 测试 swap
        p1.swap(p2);
        std::cout << "  swap 后 p1: first=" << p1.first()
                  << ", second=\"" << p1.second() << "\"\n";
        std::cout << "  swap 后 p2: first=" << p2.first()
                  << ", second=\"" << p2.second() << "\"\n\n";
    }

    // --- Pair<double, char> ---
    {
        Pair<double, char> p3(3.14, 'A');
        Pair<double, char> p4(2.72, 'Z');

        std::cout << "Pair<double, char> 测试:\n";
        std::cout << "  p3: first=" << p3.first()
                  << ", second='" << p3.second() << "'\n";
        std::cout << "  p4: first=" << p4.first()
                  << ", second='" << p4.second() << "'\n";

        // 测试 swap
        p3.swap(p4);
        std::cout << "  swap 后 p3: first=" << p3.first()
                  << ", second='" << p3.second() << "'\n";
        std::cout << "  swap 后 p4: first=" << p4.first()
                  << ", second='" << p4.second() << "'\n\n";
    }

    // --- const 访问器测试 ---
    {
        const Pair<int, std::string> cp(42, std::string("const"));
        std::cout << "const 访问器测试:\n";
        std::cout << "  cp: first=" << cp.first()
                  << ", second=\"" << cp.second() << "\"\n";
    }

    return 0;
}
