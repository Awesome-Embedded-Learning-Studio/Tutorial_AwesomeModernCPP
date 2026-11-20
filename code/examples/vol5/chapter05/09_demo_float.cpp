// demo_float.cpp —— 浮点原子加法：每一步都完整，合起来数字没动
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

int main() {
  std::atomic<float> f{16777215.0f};          // 2 的 24 次方减 1
  float r1 = f.fetch_add(1.0f);
  std::printf("16777215 + 1 -> %.0f（交回旧值 %.0f），这一步动了\n", f.load(), r1);
  for (int i = 0; i < 100; ++i) f.fetch_add(1.0f);
  std::printf("再原子加 100 次 1.0 -> %.0f，卡住了\n", f.load());

  std::atomic<float> acc{16777216.0f};        // 四条线程各加 100 万次 1.0
  std::atomic<long long> ref{16777216LL};
  std::vector<std::thread> ts;
  for (int t = 0; t < 4; ++t) {
    ts.emplace_back([&] {
      for (int i = 0; i < 1000000; ++i) {
        acc.fetch_add(1.0f);
        ref.fetch_add(1);
      }
    });
  }
  for (auto& t : ts) t.join();
  std::printf("4 条线程各加 100 万次后：float=%.0f   long long=%lld\n", acc.load(), ref.load());
  return 0;
}
