// demo_lost_updates.cpp —— 两条线程各加 160000 次，30 轮里丢多少
#include <cstdio>
#include <thread>

long long counter = 0;

void work(int rounds) {
  for (int i = 0; i < rounds; ++i) ++counter;
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
    if (counter == 2LL * kRounds) {
      ++correct;
    } else {
      std::printf("trial %2d: printed %lld, lost %lld\n",
                  t, counter, 2LL * kRounds - counter);
    }
  }
  std::printf("%d of %d trials printed the expected %d\n",
              correct, kTrials, 2 * kRounds);
}
