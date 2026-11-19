// exp_g_falsesharing.cpp —— 各写各的两个计数器，挤一条行对一人一条行
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <latch>
#include <new>
#include <thread>
#include <vector>

template <class T>
inline void do_not_optimize(T& value) {
  asm volatile("" : "+m,r"(value) : : "memory");
}

constexpr long long kPerThread = 20000000LL;   // 每线程两千万次加一

struct alignas(64) Packed { volatile long long a = 0; volatile long long b = 0; };
struct alignas(64) Slot   { volatile long long v = 0; };

template <class F, class G>
double run_two(F&& body1, G&& body2) {
  using clock = std::chrono::steady_clock;
  std::latch ready{2};
  std::latch go{1};
  std::thread t1([&] { ready.count_down(); go.wait(); body1(); });
  std::thread t2([&] { ready.count_down(); go.wait(); body2(); });
  ready.wait();
  auto t0 = clock::now();
  go.count_down();
  t1.join();
  t2.join();
  auto t1c = clock::now();
  return std::chrono::duration<double, std::milli>(t1c - t0).count();
}

int main(int argc, char** argv) {
  std::printf("sizeof(Packed)=%zu sizeof(Slot)=%zu\n", sizeof(Packed), sizeof(Slot));
  Packed paddr{};
  std::array<Slot, 2> saddr{};
  std::printf("packed a->b delta=%lld  slot0->slot1 delta=%lld\n",
      (long long)((char*)&paddr.b - (char*)&paddr.a),
      (long long)((char*)&saddr[1].v - (char*)&saddr[0].v));
  bool do_serial = true, do_packed = true, do_slot = true;
  int rounds = 3;
  if (argc > 1) {   // 单发模式留给 perf c2c 一类的工具
    do_serial = do_packed = do_slot = false;
    if (argv[1][0] == 's' && argv[1][1] == 'e') do_serial = true;
    else if (argv[1][0] == 'p') do_packed = true;
    else if (argv[1][0] == 's') do_slot = true;
    if (argc > 2) rounds = std::atoi(argv[2]);
  }
  if (do_serial) {
    for (int r = 0; r < rounds; ++r) {
      Packed packed{};
      auto t0 = std::chrono::steady_clock::now();
      for (long long i = 0; i < kPerThread; ++i) packed.a = packed.a + 1;
      for (long long i = 0; i < kPerThread; ++i) packed.b = packed.b + 1;
      auto t1 = std::chrono::steady_clock::now();
      std::printf("serial round=%d %.2fms\n", r,
                  std::chrono::duration<double, std::milli>(t1 - t0).count());
      do_not_optimize(packed.a); do_not_optimize(packed.b);
    }
  }
  if (do_packed) {
    for (int r = 0; r < rounds; ++r) {
      Packed packed{};
      double ms = run_two([&] { for (long long i = 0; i < kPerThread; ++i) packed.a = packed.a + 1; },
                          [&] { for (long long i = 0; i < kPerThread; ++i) packed.b = packed.b + 1; });
      std::printf("packed round=%d %.2fms\n", r, ms);
      do_not_optimize(packed.a); do_not_optimize(packed.b);
    }
  }
  if (do_slot) {
    for (int r = 0; r < rounds; ++r) {
      std::array<Slot, 2> slots{};
      double ms = run_two([&] { for (long long i = 0; i < kPerThread; ++i) slots[0].v = slots[0].v + 1; },
                          [&] { for (long long i = 0; i < kPerThread; ++i) slots[1].v = slots[1].v + 1; });
      std::printf("slot   round=%d %.2fms\n", r, ms);
      do_not_optimize(slots[0].v); do_not_optimize(slots[1].v);
    }
  }
  return 0;
}
