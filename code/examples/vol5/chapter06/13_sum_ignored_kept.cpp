// sum_ignored_kept.cpp —— 编译器把没人看的基准删了个干净
#include <chrono>
#include <cstdio>

// 对应 benchmark::DoNotOptimize 的写法：非 const 引用，读写约束
template <class T>
inline void do_not_optimize(T& value) {
  asm volatile("" : "+m,r"(value) : : "memory");
}

static long long sum_ignored(long long n) {
  long long s = 0;
  for (long long i = 0; i < n; ++i) s += i % 7;
  return s;                          // 调用方不要返回值
}

static long long sum_kept(long long n) {
  long long s = 0;
  for (long long i = 0; i < n; ++i) s += i % 7;
  do_not_optimize(s);                // 结果交给防优化函数看一眼
  return s;
}

int main() {
  const long long n = 100000000LL;
  using clock = std::chrono::steady_clock;
  for (int r = 0; r < 3; ++r) {
    auto a0 = clock::now();
    sum_ignored(n);                  // 返回值看都不看
    auto a1 = clock::now();
    sum_kept(n);
    auto a2 = clock::now();
    std::printf("round=%d ignored=%.3fms kept=%.3fms\n", r,
                std::chrono::duration<double, std::milli>(a1 - a0).count(),
                std::chrono::duration<double, std::milli>(a2 - a1).count());
  }
  return 0;
}
