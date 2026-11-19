#include <cstdio>
#include <thread>

long long counter = 0;

void work() {
  for (int i = 0; i < 100000; ++i) ++counter;
}

int main() {
  std::thread t[4];
  for (auto& x : t) x = std::thread(work);
  for (auto& x : t) x.join();
  std::printf("counter=%lld (expected 400000)\n", counter);
  return 0;
}
