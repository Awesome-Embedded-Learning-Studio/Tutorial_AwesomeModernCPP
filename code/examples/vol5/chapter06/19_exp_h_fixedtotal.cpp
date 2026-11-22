// exp_h_fixedtotal.cpp —— 总量固定四十万次，每次加一都进同一把锁
#include <chrono>
#include <cstdio>
#include <latch>
#include <mutex>
#include <thread>
#include <vector>

long long counter = 0;
std::mutex mu;

constexpr long long kTotal = 400000LL;

void op_lock(int threads) {
  const long long n = kTotal / threads;
  for (long long i = 0; i < n; ++i) {
    std::lock_guard<std::mutex> lk(mu);
    ++counter;
  }
}

template <class F>
double run_gated(int threads, F&& op) {
  using clock = std::chrono::steady_clock;
  std::latch ready{threads};
  std::latch go{1};
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t)
    pool.emplace_back([&] { ready.count_down(); go.wait(); op(threads); });
  ready.wait();
  auto t0 = clock::now();
  go.count_down();
  for (auto& t : pool) t.join();
  auto t1 = clock::now();
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
  for (int nt : {1, 2, 4, 8}) {
    for (int r = 0; r < 3; ++r) {
      counter = 0;
      double ms = run_gated(nt, op_lock);
      std::printf("threads=%d round=%d %.2fms per_op=%.0fns counter=%lld\n",
                  nt, r, ms, ms * 1e6 / kTotal, counter);
    }
  }
  return 0;
}
