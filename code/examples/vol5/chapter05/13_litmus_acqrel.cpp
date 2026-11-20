// litmus_acqrel.cpp —— 两条线程互测 x、y 两端的读写次序，内存序编译期三选一
// 架子与 5.2 的 litmus 相同：放行哨兵、应答只增不减、重置压在放行之前、绑核 0 与 8
// 用法: g++ -std=c++20 -O2 -pthread -DORDER_MODE=0|1|2 litmus_acqrel.cpp
// 0 = relaxed   1 = release/acquire   2 = seq_cst
#include <atomic>
#include <cstdio>
#include <pthread.h>
#include <thread>

#if ORDER_MODE == 0
#define SORD std::memory_order_relaxed
#define LORD std::memory_order_relaxed
#define NAME "relaxed"
#elif ORDER_MODE == 1
#define SORD std::memory_order_release
#define LORD std::memory_order_acquire
#define NAME "release/acquire"
#elif ORDER_MODE == 2
#define SORD std::memory_order_seq_cst
#define LORD std::memory_order_seq_cst
#define NAME "seq_cst"
#endif

struct alignas(64) Slot { std::atomic<int> v{0}; };   // x、y 各住一条缓存行
static Slot sx, sy;
static std::atomic<int>& x = sx.v;
static std::atomic<int>& y = sy.v;

// 动作对单独装一个函数，objdump 对照三版指令时一眼可见
__attribute__((noinline)) int probe(std::atomic<int>& w, std::atomic<int>& r) {
  w.store(1, SORD);
  return r.load(LORD);
}

struct alignas(64) Hand {
  std::atomic<long> round{1};   // 放行哨兵，主线程每轮推一号，唯一的起跑口
  std::atomic<long> ack{0};     // 应答，2r 表示读到 1，2r+1 表示读到 0，只增不减
};
static Hand ha, hb;

static void pin(std::thread& t, int cpu) {   // glibc 绑核接口（_np，不可移植），固定实验条件用
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  pthread_setaffinity_np(t.native_handle(), sizeof(set), &set);
}

int main() {
  const long kTrials = 10000000;
  long hits = 0;

  std::thread ta([&] {
    for (long r = 1; r <= kTrials; ) {
      if (ha.round.load(std::memory_order_relaxed) < r) continue;
      int got = probe(x, y);              // 写 x，读 y
      ha.ack.store(2 * r + (got == 0 ? 1 : 0), std::memory_order_release);
      ++r;
    }
  });
  std::thread tb([&] {
    for (long r = 1; r <= kTrials; ) {
      if (hb.round.load(std::memory_order_relaxed) < r) continue;
      int got = probe(y, x);              // 写 y，读 x
      hb.ack.store(2 * r + (got == 0 ? 1 : 0), std::memory_order_release);
      ++r;
    }
  });

  pin(ta, 0);
  pin(tb, 8);

  for (long r = 1; r <= kTrials; ++r) {
    if (r > 1) {   // 上一轮应答收齐才开下一轮，重置压在放行之前
      while (ha.ack.load(std::memory_order_acquire) < 2 * (r - 1)) {}
      while (hb.ack.load(std::memory_order_acquire) < 2 * (r - 1)) {}
    }
    x.store(0, std::memory_order_relaxed);
    y.store(0, std::memory_order_relaxed);
    ha.round.store(r, std::memory_order_seq_cst);
    hb.round.store(r, std::memory_order_seq_cst);
    long aa = 0, ba = 0;
    while ((aa = ha.ack.load(std::memory_order_acquire)) < 2 * r) {}
    while ((ba = hb.ack.load(std::memory_order_acquire)) < 2 * r) {}
    if ((aa & 1) && (ba & 1)) ++hits;   // 两边同时读到旧值 0 才记命中
  }

  ta.join();
  tb.join();
  std::printf("%s: %ld of %ld rounds saw r1=0 and r2=0\n", NAME, hits, kTrials);
}
