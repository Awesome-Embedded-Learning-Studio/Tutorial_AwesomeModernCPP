// sum_range.cpp —— 同一份求和活：串行基线对 8 线程动态领活
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

// 对应 benchmark::DoNotOptimize 的写法：非 const 引用，读写约束
template <class T>
inline void do_not_optimize(T& value) {
  asm volatile("" : "+m,r"(value) : : "memory");
}

long long sum_range(long long from, long long to) {
  long long s = 0;
  for (long long i = from; i < to; ++i) s += i % 7;
  do_not_optimize(s);
  return s;
}

constexpr int kThreads = 8;
constexpr int kChunks = 8;

// 并发版：区间切成若干块，8 条线程用原子下标动态领活，谁空谁领
double run_parallel8(long long n) {
  using clock = std::chrono::steady_clock;
  std::atomic<int> next{0};
  long long slots[kThreads] = {};
  std::vector<std::thread> pool;
  auto t0 = clock::now();   // 计时含线程进出场，正文小数据读数就是这么量的
  for (int t = 0; t < kThreads; ++t) {
    pool.emplace_back([&, t] {
      long long s = 0;
      for (;;) {
        int c = next.fetch_add(1, std::memory_order_relaxed);
        if (c >= kChunks) break;
        long long from = c * n / kChunks;
        long long to = (c + 1) * n / kChunks;
        for (long long i = from; i < to; ++i) s += i % 7;
      }
      slots[t] = s;
    });
  }
  for (auto& t : pool) t.join();
  auto t1 = clock::now();
  long long total = 0;
  for (long long v : slots) total += v;
  do_not_optimize(total);
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

double run_serial(long long n) {
  using clock = std::chrono::steady_clock;
  auto t0 = clock::now();
  long long s = sum_range(0, n);
  auto t1 = clock::now();
  do_not_optimize(s);
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
  const long long n = 1000000;
  for (int r = 0; r < 3; ++r) {
    double s = run_serial(n);
    double p = run_parallel8(n);
    std::printf("n=%lld round=%d serial=%.2fms parallel8=%.2fms speedup=%.2fx\n",
                n, r, s, p, s / p);
  }
  return 0;
}
