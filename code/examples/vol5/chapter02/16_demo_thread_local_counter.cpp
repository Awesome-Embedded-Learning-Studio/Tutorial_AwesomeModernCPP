// demo_thread_local_counter.cpp —— 每条线程一份计数，最后汇总，锁退场
#include <cstdio>
#include <thread>

thread_local long long local_counter = 0;

void work(int rounds, long long* slot, const char* name) {
  for (int i = 0; i < rounds; ++i) ++local_counter;
  std::printf("%s: value %lld at %p\n", name, local_counter,
              static_cast<void*>(&local_counter));
  *slot = local_counter;
}

int main() {
  const int kRounds = 160000;
  const int kTrials = 30;
  int correct = 0;
  long long a_sum = 0, b_sum = 0;
  for (int t = 0; t < kTrials; ++t) {
    long long a = 0, b = 0;
    std::thread ta(work, kRounds, &a, "a");
    std::thread tb(work, kRounds, &b, "b");
    ta.join();
    tb.join();
    if (a + b == 2LL * kRounds) ++correct;
    a_sum = a;
    b_sum = b;
  }
  (void)a_sum;
  (void)b_sum;
  std::printf("%d of %d trials summed to %d\n", correct, kTrials, 2 * kRounds);
  std::printf("main's own copy: %lld\n", local_counter);
  return 0;
}
