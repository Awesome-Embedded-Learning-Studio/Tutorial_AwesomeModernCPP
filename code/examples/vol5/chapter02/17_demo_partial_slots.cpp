// demo_partial_slots.cpp —— 各写各的元素，join 之后汇总，全程无锁
#include <cstdio>
#include <thread>
#include <vector>

int main() {
  std::vector<long long> partial(2, 0);
  const int rounds = 160000;
  std::thread a([&] { for (int i = 0; i < rounds; ++i) ++partial[0]; });
  std::thread b([&] { for (int i = 0; i < rounds; ++i) ++partial[1]; });
  a.join();
  b.join();
  std::printf("total = %lld\n", partial[0] + partial[1]);
  return 0;
}
