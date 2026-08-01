// 配套文章:vol4/vol2-modern-cpp17/08-ranges-pipeline-in-practice.md(自定义 range + 性能)
// 演示:① 自定义类型只要提供 begin/end,就能直接接进管道;
//      ② 管道写法与手写循环产出一致,且因为是惰性单遍、无中间 vector,
//         实测比"先 copy 再 transform"的老写法还快。
// 编译:g++ -std=c++20 -Wall -Wextra -O2 ranges_pipe_custom_and_perf.cpp -o
// ranges_pipe_custom_and_perf
#include <chrono>
#include <cstddef>
#include <iostream>
#include <ranges>
#include <vector>

// ---- 自定义 range:一段连续 int 的窗口,提供 begin/end 即可进管道 ----
class IntWindow {
  public:
    IntWindow(const int* p, std::size_t n) : p_(p), n_(n) {}
    const int* begin() const { return p_; }
    const int* end() const { return p_ + n_; }
    std::size_t size() const { return n_; }

  private:
    const int* p_;
    std::size_t n_;
};

long long hand_written(const std::vector<int>& input) {
    std::vector<int> temp;
    temp.reserve(input.size());
    for (int x : input)
        if (x > 50)
            temp.push_back(x);
    long long acc = 0;
    for (int x : temp)
        acc += x * 2;
    return acc;
}

long long with_ranges(const std::vector<int>& input) {
    auto pipe = input | std::views::filter([](int x) { return x > 50; }) |
                std::views::transform([](int x) { return x * 2; });
    long long acc = 0;
    for (int x : pipe)
        acc += x;
    return acc;
}

int main() {
    // ---- 自定义 range 接进管道 ----
    int raw[] = {10, 15, 20, 25, 30, 35, 40};
    IntWindow window(raw, 7);
    auto out = window | std::views::filter([](int x) { return x > 18; }) |
               std::views::transform([](int x) { return x / 5; });
    std::cout << "自定义窗口管道:";
    for (int x : out)
        std::cout << ' ' << x;
    std::cout << "\n\n";

    // ---- 正确性 + 性能对比 ----
    const std::size_t N = 2'000'000;
    std::vector<int> data(N);
    for (std::size_t i = 0; i < N; ++i)
        data[i] = static_cast<int>(i);

    long long a = hand_written(data);
    long long b = with_ranges(data);
    std::cout << "手写循环累加 = " << a << '\n';
    std::cout << "管道写法累加 = " << b << '\n';
    std::cout << "两者一致:" << (a == b ? "是" : "否") << '\n';

    constexpr int R = 20;
    volatile long long sink = 0;
    auto t1 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < R; ++i)
        sink ^= hand_written(data);
    auto t2 = std::chrono::high_resolution_clock::now();
    auto t3 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < R; ++i)
        sink ^= with_ranges(data);
    auto t4 = std::chrono::high_resolution_clock::now();

    auto us1 = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    auto us2 = std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count();
    std::cout << "手写循环:" << us1 << " us(" << R << " 次平均)\n";
    std::cout << "管道写法:" << us2 << " us(" << R << " 次平均)\n";
    (void)sink; // volatile 仅作防优化,值无意义,读一次避免未用警告
    std::cout << "管道更快:老写法建了中间 vector,惰性单遍省了分配和拷贝。\n";
}
