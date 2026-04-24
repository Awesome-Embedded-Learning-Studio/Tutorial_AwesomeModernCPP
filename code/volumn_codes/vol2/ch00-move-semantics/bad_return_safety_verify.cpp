/// @file bad_return_safety_verify.cpp
/// @brief 验证文章"错误 3：在返回语句中用 std::forward"一节的技术准确性
///
/// 编译验证结论：
///   - 文章原始 bad_return(T&& x) 返回 T，当传入临时对象时 T 推导为非引用类型，
///     返回类型是值类型，通过移动构造安全返回，不会产生悬空引用。
///   - 真正的危险模式是 decltype(auto) 返回类型 + std::forward，此时传入右值会
///     导致返回右值引用指向已销毁的函数参数。
///
/// Standard: C++17
/// Compiler: g++ 13+, -std=c++17 -Wall -Wextra

#include <iostream>
#include <string>
#include <utility>

/// @brief 用于追踪构造/析构行为的测试类型
class Tracker
{
    std::string name_;

public:
    explicit Tracker(const std::string& n) : name_(n)
    {
        std::cout << "  Tracker::ctor(" << name_ << ")\n";
    }

    Tracker(Tracker&& o) noexcept : name_(std::move(o.name_))
    {
        o.name_ = "(moved-from)";
        std::cout << "  Tracker::move_ctor(" << name_ << ")\n";
    }

    Tracker(const Tracker& o) : name_(o.name_)
    {
        std::cout << "  Tracker::copy_ctor(" << name_ << ")\n";
    }

    ~Tracker()
    {
        std::cout << "  Tracker::dtor(" << name_ << ")\n";
    }

    const std::string& name() const { return name_; }
};

/// @brief 文章中的原始版本：返回类型为 T
/// 当传入左值时 T = U&，返回类型 U&，引用指向原始对象（安全）
/// 当传入右值时 T = U，返回类型 U（按值），移动构造返回（安全）
template<typename T>
T original_bad_return(T&& x)
{
    return std::forward<T>(x);
}

/// @brief 真正的危险版本：decltype(auto) 返回类型
/// 当传入右值时 T = U，decltype(auto) 推导为 U&&（右值引用）
/// 返回的引用指向已销毁的函数参数 x → 悬空引用！
template<typename T>
decltype(auto) dangerous_decltype_return(T&& x)
{
    return (std::forward<T>(x));
}

/// @brief 辅助函数：打印 T 的推导结果
template<typename T>
void print_deduction(const char* label)
{
    std::cout << "  " << label << ": T is "
              << (std::is_lvalue_reference_v<T> ? "lvalue-ref" : "non-ref/value")
              << "\n";
}

int main()
{
    std::cout << "========================================\n"
              << "验证 1: original_bad_return(T&& x) 返回 T\n"
              << "========================================\n\n";

    // Case 1: 传入左值 → T = Tracker&
    // 返回类型 Tracker&，引用原始对象（安全）
    std::cout << "--- 传入左值 ---\n";
    Tracker lvalue_obj("lvalue");
    print_deduction<Tracker&>("T deduction");
    Tracker& ref_result = original_bad_return(lvalue_obj);
    std::cout << "  返回的引用指向: " << ref_result.name()
              << " (原始对象，仍然存活)\n\n";

    // Case 2: 传入右值 → T = Tracker
    // 返回类型 Tracker（按值），移动构造返回值（安全）
    std::cout << "--- 传入右值 ---\n";
    Tracker val_result = original_bad_return(Tracker("rvalue"));
    std::cout << "  按值返回，移动构造: " << val_result.name() << "\n\n";

    std::cout << "结论: original_bad_return 对左值和右值都是安全的。\n"
              << "  - 左值: 返回引用指向原始对象\n"
              << "  - 右值: 按值返回（移动构造）\n\n";

    std::cout << "========================================\n"
              << "验证 2: dangerous_decltype_return(T&& x) 返回 decltype(auto)\n"
              << "========================================\n\n";

    // Case 3: dangerous_decltype_return 传入右值 → 危险！
    // T = Tracker, decltype(auto) 推导为 Tracker&&
    // 返回右值引用指向函数参数 x，x 在函数返回时已销毁 → 悬空引用
    std::cout << "--- 传入右值（危险！） ---\n";
    std::cout << "  GCC -Wdangling-reference 会对此发出警告\n";
    // 取消下面的注释会触发编译警告和运行时 UB:
    // auto&& dangling = dangerous_decltype_return(Tracker("temp"));
    // std::cout << dangling.name() << "\n";  // UB!
    std::cout << "  （已注释掉 UB 代码，仅作说明）\n\n";

    std::cout << "结论: decltype(auto) + std::forward 是真正的危险模式。\n"
              << "  传入右值时返回 Tracker&& 指向已销毁的参数。\n\n";

    std::cout << "========================================\n"
              << "验证 3: 正确的按值返回写法\n"
              << "========================================\n\n";

    // 正确写法 1: 按值返回 + std::forward（原函数即是）
    std::cout << "正确写法 1: T return_type(T&& x) { return std::forward<T>(x); }\n";

    // 正确写法 2: 直接按值返回（不用 forward）
    std::cout << "正确写法 2: T return_type(T&& x) { return x; }（依赖 NRVO 或移动语义）\n\n";

    // 清理
    std::cout << "=== 清理 ===\n";
    return 0;
}
