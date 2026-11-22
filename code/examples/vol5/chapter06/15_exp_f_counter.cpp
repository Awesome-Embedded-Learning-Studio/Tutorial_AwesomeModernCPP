// exp_f_counter.cpp —— perf stat 的两个对照：各写各的对抢同一个 atomic
#include <atomic>
#include <cstdio>
#include <latch>
#include <thread>
#include <vector>

// 对应 benchmark::DoNotOptimize 的写法：非 const 引用，读写约束
template <class T>
inline void do_not_optimize(T& value) {
  asm volatile("" : "+m,r"(value) : : "memory");
}

struct alignas(64) Slot { long long v = 0; };

constexpr long long kPerThread = 6250000LL;   // 每线程 625 万次，八条共五千万
std::atomic<long long> shared{0};
std::vector<Slot> slots(8);
std::latch go{1};

int main(int argc, char** argv) {
  bool use_shared = (argc > 1 && argv[1][0] == 's');
  std::vector<std::thread> pool;
  for (int t = 0; t < 8; ++t) {
    pool.emplace_back([t, use_shared] {
      go.wait();                     // 八条线程在闸门口排队，起跑线齐
      long long s = 0;
      for (long long i = 0; i < kPerThread; ++i) {
        if (use_shared) shared.fetch_add(1, std::memory_order_relaxed);
        else { s += 1; do_not_optimize(s); }   // 不加它，编译器把整段循环折成常量
      }
      slots[t].v = s;                // 各写各的：累加结果落进自己的槽位
    });
  }
  go.count_down();                   // 放闸
  for (auto& t : pool) t.join();
  long long total = shared.load();
  for (const auto& slot : slots) total += slot.v;
  std::printf("mode=%s total=%lld\n", use_shared ? "shared" : "private", total);
  return 0;
}
