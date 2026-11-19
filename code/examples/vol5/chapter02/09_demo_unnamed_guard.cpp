// demo_unnamed_guard.cpp —— 没有名字的 lock_guard 临时对象：锁在加一之前就放了
#include <cstdio>
#include <mutex>
#include <thread>

long long counter = 0;
std::mutex counter_mutex;

void work(int rounds) {
  for (int i = 0; i < rounds; ++i) {
    std::lock_guard<std::mutex>{counter_mutex};   // 临时对象：语句结束即析构
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
    else std::printf("trial %d: printed %lld, lost %lld\n",
                     t, counter, 2LL * kRounds - counter);
  }
  std::printf("%d of %d trials printed the expected %d\n",
              correct, kTrials, 2 * kRounds);
}
