// callable.cpp
#include <cstdio>
#include <cstring>
#include <string>

/// @brief 带阈值的范围检查函数对象
class ThresholdChecker {
private:
    int min_;
    int max_;
    int rejected_count_;

public:
    ThresholdChecker(int min_val, int max_val)
        : min_(min_val), max_(max_val), rejected_count_(0)
    {
    }

    /// @brief 检查值是否在范围内，不在范围内则增加拒绝计数
    bool operator()(int value)
    {
        if (value < min_ || value > max_) {
            ++rejected_count_;
            return false;
        }
        return true;
    }

    int rejected_count() const { return rejected_count_; }

    void reset() { rejected_count_ = 0; }
};

/// @brief 安全的布尔包装器，使用 explicit operator bool
class SafeBool {
private:
    bool value_;

public:
    explicit SafeBool(bool v) : value_(v) {}

    explicit operator bool() const { return value_; }
};

/// @brief 字符串形式的数值，支持显式转换为 int 和 const char*
class StringNumber {
private:
    char buffer_[32];

public:
    explicit StringNumber(const char* str)
    {
        std::strncpy(buffer_, str, sizeof(buffer_) - 1);
        buffer_[sizeof(buffer_) - 1] = '\0';
    }

    explicit operator int() const { return std::atoi(buffer_); }

    explicit operator const char*() const { return buffer_; }
};

int main()
{
    // --- ThresholdChecker: 函数对象 ---
    ThresholdChecker checker(0, 100);

    int test_values[] = {50, -1, 75, 200, 30, -5, 88};
    const char* labels[] = {"50", "-1", "75", "200", "30", "-5", "88"};

    std::printf("=== ThresholdChecker (0..100) ===\n");
    for (int i = 0; i < 7; ++i) {
        bool ok = checker(test_values[i]);
        std::printf("  %s -> %s\n", labels[i], ok ? "PASS" : "REJECT");
    }
    std::printf("  Rejected: %d\n", checker.rejected_count());

    // --- SafeBool: explicit operator bool ---
    std::printf("\n=== SafeBool ===\n");
    SafeBool flag_true(true);
    SafeBool flag_false(false);

    if (flag_true) {
        std::printf("  flag_true is truthy\n");
    }
    if (!flag_false) {
        std::printf("  flag_false is falsy\n");
    }

    // --- StringNumber: explicit conversion ---
    std::printf("\n=== StringNumber ===\n");
    StringNumber sn("42");
    StringNumber sn2("100");

    int val = static_cast<int>(sn);
    int val2 = static_cast<int>(sn2);
    const char* str = static_cast<const char*>(sn);

    std::printf("  StringNumber(\"42\") as int: %d\n", val);
    std::printf("  StringNumber(\"100\") as int: %d\n", val2);
    std::printf("  StringNumber(\"42\") as string: %s\n", str);
    std::printf("  Sum: %d\n", val + val2);

    return 0;
}
