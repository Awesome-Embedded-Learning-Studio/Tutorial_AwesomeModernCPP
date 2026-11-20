#include <atomic>
#include <cstdio>
#include <thread>

std::atomic<bool> go{false};
std::atomic<bool> reserved{false};
std::atomic<int> winners{0};

void contender() {
  while (!go.load(std::memory_order_relaxed)) {}
  if (!reserved.load(std::memory_order_relaxed)) {
    reserved.store(true, std::memory_order_relaxed);
    winners.fetch_add(1, std::memory_order_relaxed);
  }
}

int main() {
  const int kRounds = 10000;
  int double_wins = 0;
  for (int r = 0; r < kRounds; ++r) {
    reserved.store(false);
    winners.store(0);
    go.store(false);
    std::thread a(contender);
    std::thread b(contender);
    go.store(true);
    a.join();
    b.join();
    if (winners.load() == 2) ++double_wins;
  }
  std::printf("double wins: %d of %d rounds\n", double_wins, kRounds);
  return 0;
}
