/**
 * @file ex07_error_design.cpp
 * @brief 练习：错误处理设计方案
 *
 * 定义 Error 枚举类（FileNotFound, PermissionDenied, Timeout），
 * 实现 Expected<T> 模板（持有 value 或 error），并提供
 * read_file 函数签名返回 Expected<std::string>。
 */

#include <iostream>
#include <string>
#include <variant>

// ---- Error 枚举 ----

/// @brief 文件操作错误码
enum class Error {
    kFileNotFound,
    kPermissionDenied,
    kTimeout,
};

/// @brief 将 Error 转为可读字符串
std::string to_string(Error err)
{
    switch (err) {
    case Error::kFileNotFound:
        return "file not found";
    case Error::kPermissionDenied:
        return "permission denied";
    case Error::kTimeout:
        return "operation timed out";
    }
    return "unknown error";
}

// ---- Expected<T> 模板 ----

/// @brief 简易 Expected<T> —— 要么持有值，要么持有 Error
template <typename T>
struct Expected {
private:
    std::variant<T, Error> data_;
    bool ok_;

public:
    /// @brief 成功时构造（持有值）
    Expected(T value) : data_(std::move(value)), ok_(true) {}

    /// @brief 失败时构造（持有 Error）
    Expected(Error err) : data_(err), ok_(false) {}

    /// @brief 是否持有有效值
    bool ok() const { return ok_; }

    /// @brief 获取值（仅在 ok() 为 true 时调用）
    const T& value() const { return std::get<0>(data_); }

    /// @brief 获取错误（仅在 ok() 为 false 时调用）
    Error error() const { return std::get<1>(data_); }
};

// ---- 模拟 read_file ----

/// @brief 模拟文件读取，用路径首字母决定结果
///   首字母 'x' → kFileNotFound
///   首字母 'y' → kPermissionDenied
///   首字母 'z' → kTimeout
///   其它 → 正常返回模拟内容
Expected<std::string> read_file(const std::string& path)
{
    if (path.empty()) {
        return Error::kFileNotFound;
    }

    switch (path[0]) {
    case 'x':
        return Error::kFileNotFound;
    case 'y':
        return Error::kPermissionDenied;
    case 'z':
        return Error::kTimeout;
    default:
        return std::string("content of " + path);
    }
}

// ---- 演示 ----

int main()
{
    std::cout << "===== 错误处理设计：Expected<T> =====\n\n";

    std::string paths[] = {
        "apple.txt",    // 正常
        "xyz.dat",      // FileNotFound
        "yml.cfg",      // PermissionDenied
        "zip.tar",      // Timeout
    };

    for (const auto& p : paths) {
        auto result = read_file(p);
        std::cout << "  read_file(\"" << p << "\") -> ";
        if (result.ok()) {
            std::cout << "OK: \"" << result.value() << "\"\n";
        }
        else {
            std::cout << "Error: " << to_string(result.error()) << "\n";
        }
    }

    // 单独演示空路径
    std::cout << "  read_file(\"\") -> ";
    auto empty_result = read_file("");
    if (!empty_result.ok()) {
        std::cout << "Error: " << to_string(empty_result.error()) << "\n";
    }

    std::cout << "\n要点:\n";
    std::cout << "  1. Error 枚举清晰定义了所有可能的失败模式\n";
    std::cout << "  2. Expected<T> 比 std::optional 多了错误原因\n";
    std::cout << "  3. 调用方必须显式检查 ok()，不会意外忽略错误\n";

    return 0;
}
