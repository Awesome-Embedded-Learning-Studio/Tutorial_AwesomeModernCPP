// push_back_vs_emplace.cpp -- push_back vs emplace_back 的构造/移动/析构追踪
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>
#include <vector>

class TrackedString {
    std::string data_;

  public:
    explicit TrackedString(const char* s) : data_(s) {
        std::cout << "  [ctor from const char*] \"" << data_ << "\"\n";
    }

    TrackedString(const TrackedString& other) : data_(other.data_) {
        std::cout << "  [copy ctor] \"" << data_ << "\"\n";
    }

    TrackedString(TrackedString&& other) noexcept : data_(std::move(other.data_)) {
        std::cout << "  [move ctor] \"" << data_ << "\"\n";
    }

    ~TrackedString() { std::cout << "  [dtor] \"" << data_ << "\"\n"; }
};

int main() {
    std::cout << "=== push_back(TrackedString(\"Bob\")) ===\n";
    {
        std::vector<TrackedString> v;
        v.push_back(TrackedString("Bob"));
        std::cout << "=== done ===\n";
    }

    std::cout << "\n=== emplace_back(\"Alice\") ===\n";
    {
        std::vector<TrackedString> v;
        v.emplace_back("Alice");
        std::cout << "=== done ===\n";
    }

    return 0;
}
