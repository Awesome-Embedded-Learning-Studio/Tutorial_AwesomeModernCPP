// ex06_result_bool.cpp
// 练习：为 Result 类实现 explicit operator bool
// Result<T> 模板，持有一个值或一条错误信息。
// 通过 explicit operator bool 判断是否持有有效值。
// 编译：g++ -Wall -Wextra -std=c++17 -o ex06 ex06_result_bool.cpp

#include <iostream>
#include <string>
#include <variant>

template <typename T>
class Result {
private:
    // 用 variant 存储「值」或「错误信息」
    std::variant<T, std::string> storage_;

public:
    // 成功：持有值
    Result(T value) : storage_(std::move(value)) {}

    // 失败：持有错误信息
    Result(const char* error) : storage_(std::string(error)) {}

    // explicit bool 转换——不会隐式转成 int 等类型
    explicit operator bool() const
    {
        return std::holds_alternative<T>(storage_);
    }

    // 获取值（需先检查是否成功）
    T& value()
    {
        return std::get<T>(storage_);
    }

    const T& value() const
    {
        return std::get<T>(storage_);
    }

    // 获取错误信息
    const std::string& error() const
    {
        return std::get<std::string>(storage_);
    }
};

// 一个可能失败的除法函数
Result<double> safe_divide(double a, double b)
{
    if (b == 0.0) {
        return Result<double>("除数不能为零");
    }
    return Result<double>(a / b);
}

// 一个可能失败的字符串解析
Result<int> parse_int(const std::string& s)
{
    try {
        std::size_t pos = 0;
        int val = std::stoi(s, &pos);
        if (pos != s.size()) {
            return Result<int>("字符串中包含非数字字符");
        }
        return Result<int>(val);
    } catch (const std::exception&) {
        return Result<int>("无法解析为整数");
    }
}

// ============================================================
// main
// ============================================================
int main()
{
    // --- 除法测试 ---
    std::cout << "=== safe_divide ===\n";

    auto r1 = safe_divide(10.0, 3.0);
    if (r1) {
        std::cout << "10 / 3 = " << r1.value() << "\n";
    } else {
        std::cout << "错误: " << r1.error() << "\n";
    }

    auto r2 = safe_divide(5.0, 0.0);
    if (r2) {
        std::cout << "5 / 0 = " << r2.value() << "\n";
    } else {
        std::cout << "错误: " << r2.error() << "\n";
    }

    // --- 解析测试 ---
    std::cout << "\n=== parse_int ===\n";

    auto r3 = parse_int("42");
    if (r3) {
        std::cout << "\"42\" -> " << r3.value() << "\n";
    } else {
        std::cout << "错误: " << r3.error() << "\n";
    }

    auto r4 = parse_int("abc");
    if (r4) {
        std::cout << "\"abc\" -> " << r4.value() << "\n";
    } else {
        std::cout << "错误: " << r4.error() << "\n";
    }

    auto r5 = parse_int("12ab");
    if (r5) {
        std::cout << "\"12ab\" -> " << r5.value() << "\n";
    } else {
        std::cout << "错误: " << r5.error() << "\n";
    }

    // --- explicit 测试 ---
    // 以下代码不应该编译通过：
    //   int x = r1;  // 错误：没有隐式转换
    //   if (r1 == true) {}  // 错误：不能直接比较
    // 但可以用于 if/while 等上下文：
    std::cout << "\nexplicit operator bool 测试通过\n";

    return 0;
}
