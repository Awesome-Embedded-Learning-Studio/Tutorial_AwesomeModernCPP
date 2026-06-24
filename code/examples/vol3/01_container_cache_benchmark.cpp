// Standard: C++20
// 容器选择 · 内存局部性 benchmark：vector（连续内存）vs list（节点式）遍历耗时
// 两者遍历都是 O(n)、每次加法都是 O(1)，但 vector 的连续内存吃满 cache，
// list 的每个节点都要单独访存——实测两边耗时差几倍，这就是「默认用 vector」的底层理由。
#include <chrono>
#include <cstdio>
#include <list>
#include <vector>

int main() {
    constexpr int N = 1'000'000;
    std::vector<int> v(N);
    std::list<int> l;
    for (int i = 0; i < N; ++i) {
        v[i] = i;
        l.push_back(i);
    }

    volatile long long sink = 0;

    auto t0 = std::chrono::high_resolution_clock::now();
    long long sv = 0;
    for (auto x : v) {
        sv += x;
    }
    sink += sv;
    auto t1 = std::chrono::high_resolution_clock::now();

    long long sl = 0;
    for (auto x : l) {
        sl += x;
    }
    sink += sl;
    auto t2 = std::chrono::high_resolution_clock::now();

    auto us_v = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    auto us_l = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    std::printf("vector 遍历 %lld us, list 遍历 %lld us, list 慢 %.2fx\n", us_v, us_l,
                us_v ? (double)us_l / us_v : 0.0);
    return 0;
}
