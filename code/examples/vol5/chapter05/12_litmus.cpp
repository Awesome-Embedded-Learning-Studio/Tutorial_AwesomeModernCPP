#include <atomic>
#include <cstdio>
#include <pthread.h>
#include <sched.h>
#include <thread>

#ifndef USE_RELAXED
#define USE_RELAXED 0
#endif
constexpr bool kRelaxed = USE_RELAXED;

struct alignas(64) Slot { std::atomic<int> v{0}; };   // x、y 各住一条缓存行
static Slot sx, sy;
static std::atomic<int>& x = sx.v;
static std::atomic<int>& y = sy.v;

struct alignas(64) Hand {
  std::atomic<long> round{1};   // 主线程每轮推一号，是唯一放行口
  std::atomic<long> ack{0};     // 回 2r（读到 1）或 2r+1（读到 0），只增不减
};
static Hand ha, hb;

static void pin(std::thread& t, int cpu) {   // glibc 的绑核接口（_np，不可移植），拿来固定实验条件
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  pthread_setaffinity_np(t.native_handle(), sizeof(set), &set);
}

int main() {
  const auto ord = kRelaxed ? std::memory_order_relaxed
                            : std::memory_order_seq_cst;
  const long kTrials = 10000000;
  long hits = 0;

  std::thread ta([&] {
    for (long r = 1; r <= kTrials; ) {
      if (ha.round.load(std::memory_order_relaxed) < r) continue;
      x.store(1, ord);
      int got = y.load(ord);
      ha.ack.store(2 * r + (got == 0 ? 1 : 0), std::memory_order_release);
      ++r;
    }
  });
  std::thread tb([&] {
    for (long r = 1; r <= kTrials; ) {
      if (hb.round.load(std::memory_order_relaxed) < r) continue;
      y.store(1, ord);
      int got = x.load(ord);
      hb.ack.store(2 * r + (got == 0 ? 1 : 0), std::memory_order_release);
      ++r;
    }
  });

  pin(ta, 0);   // 0 号与 8 号逻辑核，本机拓扑分属物理核 0 与 4
  pin(tb, 8);

  for (long r = 1; r <= kTrials; ++r) {
    if (r > 1) {   // 上一轮的应答收讫，才轮到重置与放行
      while (ha.ack.load(std::memory_order_acquire) < 2 * (r - 1)) {}
      while (hb.ack.load(std::memory_order_acquire) < 2 * (r - 1)) {}
    }
    x.store(0, std::memory_order_relaxed);
    y.store(0, std::memory_order_relaxed);
    ha.round.store(r, std::memory_order_seq_cst);   // 放行，顺带把重置压出去
    hb.round.store(r, std::memory_order_seq_cst);
    long aa = 0, ba = 0;
    while ((aa = ha.ack.load(std::memory_order_acquire)) < 2 * r) {}
    while ((ba = hb.ack.load(std::memory_order_acquire)) < 2 * r) {}
    if ((aa & 1) && (ba & 1)) ++hits;
  }

  ta.join();
  tb.join();
  std::printf("%s: %ld of %ld rounds saw r1=0 and r2=0\n",
              kRelaxed ? "relaxed" : "seq_cst", hits, kTrials);
}
