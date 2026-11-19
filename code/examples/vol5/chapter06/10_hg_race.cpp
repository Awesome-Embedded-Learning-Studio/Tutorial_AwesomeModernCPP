#include <cstdio>
#include <thread>

long long counter = 0;

void work() {
  for (int i = 0; i < 20000; ++i) ++counter;
}

int main() {
  std::thread a(work);
  std::thread b(work);
  a.join();
  b.join();
  std::printf("counter=%lld (expected 40000)\n", counter);
  return 0;
}
