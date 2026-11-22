// timing_harness.cpp —— 本篇用的计时架子：一次一轮，读数逐轮交出
#include <chrono>
#include <cstdio>
#include <vector>

// 对应 benchmark::DoNotOptimize 的 v1.10.0 写法：非 const 引用，读写约束
template <class T>
inline void do_not_optimize(T& value) {
  asm volatile("" : "+m,r"(value) : : "memory");
}

// 官方 v1.10.0 已把 ClobberMemory 改成 atomic_signal_fence（只约束信号处理器那一路内存次序的栅栏），这里摆的是旧式空汇编写法
inline void clobber_memory() {
  asm volatile("" ::: "memory");
}

// 跑 reps 轮，交回每一轮的毫秒数
template <class F>
std::vector<double> measure_ms(F&& f, int reps) {
  using clock = std::chrono::steady_clock;
  std::vector<double> out;
  for (int r = 0; r < reps; ++r) {
    auto t0 = clock::now();
    f();
    auto t1 = clock::now();
    out.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  return out;
}

int main() {
  // 一亿次求和连跑六轮，每轮读数原样交出来（正文“预热与尾差”一节的实验）
  const long long n = 100000000LL;
  auto rounds = measure_ms([&] {
    long long s = 0;
    for (long long i = 0; i < n; ++i) s += i % 7;
    do_not_optimize(s);
  }, 6);
  for (int r = 0; r < (int)rounds.size(); ++r)
    std::printf("round=%d %.1fms\n", r, rounds[r]);
  return 0;
}
