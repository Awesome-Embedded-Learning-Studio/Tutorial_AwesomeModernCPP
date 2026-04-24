// string_concat_move.cpp -- 验证字符串拼接中移动语义的作用
// 模拟 C++11/14 行为（operator+ 返回命名变量，触发隐式移动）
// 编译: g++ -std=c++17 -O0 -o /tmp/string_concat_move string_concat_move.cpp
// 对比: g++ -std=c++17 -O2 -o /tmp/string_concat_move string_concat_move.cpp
#include <iostream>
#include <string>

/// @brief 带追踪的字符串类，用于观察构造/拷贝/移动行为
struct TrackedString {
    std::string data_;

    explicit TrackedString(const char* s) : data_(s)
    {
        std::cout << "  [ctor] \"" << data_ << "\"\n";
    }

    TrackedString(const TrackedString& other) : data_(other.data_)
    {
        std::cout << "  [copy ctor] \"" << data_ << "\"\n";
    }

    TrackedString(TrackedString&& other) noexcept : data_(std::move(other.data_))
    {
        std::cout << "  [move ctor] \"" << data_ << "\" (from \"" << other.data_ << "\")\n";
    }

    TrackedString& operator=(const TrackedString&) = delete;
    TrackedString& operator=(TrackedString&&) = delete;

    /// @brief 返回命名局部变量——C++11/14 下会触发隐式移动
    friend TrackedString operator+(TrackedString a, const TrackedString& b)
    {
        a.data_ += b.data_;
        std::cout << "  [operator+] producing \"" << a.data_ << "\"\n";
        return a;
    }
};

/// @brief 模拟日志消息构建：链式字符串拼接
TrackedString build_message(
    const TrackedString& level,
    const TrackedString& module,
    const TrackedString& detail)
{
    TrackedString msg = level + TrackedString("] ")
                      + module + TrackedString(": ") + detail;
    std::cout << "  [build_message] msg = \"" << msg.data_ << "\"\n";
    return msg;
}

int main()
{
    TrackedString level("[");
    TrackedString module("Network");
    TrackedString detail("timeout");

    std::cout << "--- start ---\n";
    TrackedString log = build_message(level, module, detail);
    std::cout << "--- done ---\n";
    std::cout << "result: " << log.data_ << "\n";
    return 0;
}
