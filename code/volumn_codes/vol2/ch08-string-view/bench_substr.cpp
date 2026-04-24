#include <string>
#include <string_view>
#include <chrono>
#include <iostream>
#include <random>

class Timer {
public:
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}

    double elapsed_ms() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start_).count();
    }

private:
    std::chrono::high_resolution_clock::time_point start_;
};

constexpr int kStringLength = 10000;
constexpr int kSubstrLen = 50;
constexpr int kIterations = 100000;

// 生成随机字符串
std::string make_long_string(int len) {
    std::string s(len, 'a');
    for (int i = 0; i < len; ++i) {
        s[i] = static_cast<char>('a' + (i % 26));
    }
    return s;
}

void bench_string_substr(const std::string& s) {
    Timer t;
    volatile std::size_t sink = 0;  // 防止优化掉
    for (int i = 0; i < kIterations; ++i) {
        auto sub = s.substr(i % (s.size() - kSubstrLen), kSubstrLen);
        sink += sub.size();
    }
    std::cout << "std::string::substr:   "
              << t.elapsed_ms() << " ms (sink=" << sink << ")\n";
}

void bench_string_view_substr(std::string_view sv) {
    Timer t;
    volatile std::size_t sink = 0;
    for (int i = 0; i < kIterations; ++i) {
        auto sub = sv.substr(i % (sv.size() - kSubstrLen), kSubstrLen);
        sink += sub.size();
    }
    std::cout << "string_view::substr:   "
              << t.elapsed_ms() << " ms (sink=" << sink << ")\n";
}

int main() {
    auto long_str = make_long_string(kStringLength);
    bench_string_substr(long_str);
    bench_string_view_substr(long_str);
    return 0;
}
