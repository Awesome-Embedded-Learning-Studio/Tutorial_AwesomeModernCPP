// demo_ledger_one_lock.cpp —— 一把锁盖住两边：不变量不再露缝
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

struct Ledger {
  std::mutex m;
  long a = 700;
  long b = 300;

  void transfer(long amount) {
    std::lock_guard<std::mutex> lock(m);
    a -= amount;
    b += amount;
  }
  long total() {
    std::lock_guard<std::mutex> lock(m);
    return a + b;
  }
};

void run_round(int round) {
  Ledger ledger;
  std::atomic<bool> moving{true};
  long long bad = 0, samples = 0;

  std::thread mover([&] {
    for (int i = 0; i < 40; ++i) {
      ledger.transfer(10);
      std::this_thread::sleep_for(1ms);   // 和 Wallet 版的节奏对齐
    }
    moving.store(false);
  });
  std::thread observer([&] {
    while (moving.load()) {
      long t = ledger.total();
      ++samples;
      if (t != 1000) ++bad;
    }
  });
  mover.join();
  observer.join();
  std::printf("round %d: one lock for both sides: %lld of %lld samples saw a total other than 1000\n",
              round, bad, samples);
  std::printf("round %d final: A = %ld, B = %ld, total = %ld\n",
              round, ledger.a, ledger.b, ledger.a + ledger.b);
}

int main() {
  for (int r = 1; r <= 2; ++r) run_round(r);
  return 0;
}
