// demo_mutex_counter.cpp —— 给上一篇丢数的计数器配一把锁
#include <cstdio>
#include <mutex>
#include <thread>

long long counter = 0;
std::mutex counter_mutex;

void work(int rounds) {
  for (int i = 0; i < rounds; ++i) {
    std::lock_guard<std::mutex> lock(counter_mutex);
    ++counter;
  }
}

int main() {
  const int kRounds = 160000;
  const int kTrials = 30;
  int correct = 0;
  for (int t = 0; t < kTrials; ++t) {
    counter = 0;
    std::thread a(work, kRounds);
    std::thread b(work, kRounds);
    a.join();
    b.join();
    if (counter == 2LL * kRounds) ++correct;
  }
  std::printf("%d of %d trials printed the expected %d\n",
              correct, kTrials, 2 * kRounds);
}
