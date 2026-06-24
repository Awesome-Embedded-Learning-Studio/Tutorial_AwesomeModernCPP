// Standard: C++20
// string_view 零拷贝传参 vs const string& 的性能对比
// 同一个 90 字节 payload（稳超 SSO），紧循环多次调用：
// const string& 路径每次都构造临时 string，string_view 路径零分配——差距来自这。
#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>

static const char* kPayload =
    "The quick brown fox jumps over the lazy dog - a non-trivial string payload.";

long count_vowels_ref(const std::string& s) {
    long n = 0;
    for (char c : s) {
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u')
            ++n;
    }
    return n;
}

long count_vowels_sv(std::string_view sv) {
    long n = 0;
    for (char c : sv) {
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u')
            ++n;
    }
    return n;
}

int main() {
    constexpr int kIters = 50'000'000;
    volatile long sink = 0;

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i)
        sink += count_vowels_ref(kPayload);
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i)
        sink += count_vowels_sv(kPayload);
    auto t2 = std::chrono::steady_clock::now();

    auto ms_ref = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    auto ms_sv = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    std::printf("const string& path: %lld ms\n", static_cast<long long>(ms_ref));
    std::printf("string_view  path: %lld ms\n", static_cast<long long>(ms_sv));
    std::printf("ratio (ref/sv): %.2fx\n", ms_sv ? static_cast<double>(ms_ref) / ms_sv : 0.0);
    return 0;
}
