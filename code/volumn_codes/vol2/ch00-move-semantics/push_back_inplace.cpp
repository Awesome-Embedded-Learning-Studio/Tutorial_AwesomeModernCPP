// push_back_inplace.cpp -- 验证 push_back 与 emplace_back 的行为差异
// push_back 传入临时对象时会先构造再移动，emplace_back 才是真正的就地构造
// 编译: g++ -std=c++17 -O0 -o /tmp/push_back_inplace push_back_inplace.cpp
// 对比: g++ -std=c++17 -O2 -o /tmp/push_back_inplace push_back_inplace.cpp
#include <iostream>
#include <string>
#include <vector>

/// @brief 带追踪的字符串类，用于观察构造/拷贝/移动行为
struct TrackedString {
    std::string data_;

    explicit TrackedString(const char* s) : data_(s)
    {
        std::cout << "  [ctor from const char*] \"" << data_ << "\"\n";
    }

    TrackedString(const TrackedString& other) : data_(other.data_)
    {
        std::cout << "  [copy ctor] \"" << data_ << "\"\n";
    }

    TrackedString(TrackedString&& other) noexcept : data_(std::move(other.data_))
    {
        std::cout << "  [move ctor] \"" << data_ << "\"\n";
    }

    ~TrackedString()
    {
        std::cout << "  [dtor] \"" << data_ << "\"\n";
    }

    TrackedString& operator=(const TrackedString&) = delete;
    TrackedString& operator=(TrackedString&&) = delete;
};

int main()
{
    std::cout << "=== push_back(TrackedString(\"Bob\")) ===\n";
    std::vector<TrackedString> vec;
    vec.push_back(TrackedString("Bob"));
    std::cout << "=== done ===\n";

    std::cout << "\n=== emplace_back(\"Alice\") ===\n";
    std::vector<TrackedString> vec2;
    vec2.emplace_back("Alice");
    std::cout << "=== done ===\n";

    return 0;
}
