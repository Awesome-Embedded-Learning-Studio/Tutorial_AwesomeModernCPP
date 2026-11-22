// demo_wallet_split_locks.cpp —— 每个钱包一把锁：和不变量跨了两把锁
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

struct Wallet {
  std::mutex m;
  long balance;
};

void move(Wallet& from, Wallet& to, long amount) {
  {
    std::lock_guard<std::mutex> lock(from.m);
    from.balance -= amount;
  }
  std::this_thread::sleep_for(1ms);   // 把两步之间的窗口放大
  {
    std::lock_guard<std::mutex> lock(to.m);
    to.balance += amount;
  }
  std::this_thread::sleep_for(1ms);   // 让完整状态也占一段窗口
}

long total(Wallet& a, Wallet& b) {
  long sum = 0;
  {
    std::lock_guard<std::mutex> lock(a.m);
    sum += a.balance;
  }
  {
    std::lock_guard<std::mutex> lock(b.m);
    sum += b.balance;
  }
  return sum;
}

void run_round(int round) {
  Wallet a{};
  Wallet b{};
  a.balance = 700;
  b.balance = 300;
  std::atomic<bool> moving{true};
  long long bad = 0, samples = 0;
  int printed = 0;

  std::thread mover([&] {
    for (int i = 0; i < 40; ++i) move(a, b, 10);
    moving.store(false);
  });
  std::thread observer([&] {
    while (moving.load()) {
      long t = total(a, b);
      ++samples;
      if (t != 1000) {
        ++bad;
        if (printed < 4) {
          std::printf("round %d sample %lld: total %ld\n", round, samples, t);
          ++printed;
        }
      }
    }
  });
  mover.join();
  observer.join();
  std::printf("round %d: per-object locks: %lld of %lld samples saw a total other than 1000\n",
              round, bad, samples);
  std::printf("round %d final: A = %ld, B = %ld, total = %ld\n",
              round, a.balance, b.balance, a.balance + b.balance);
}

int main() {
  for (int r = 1; r <= 2; ++r) run_round(r);
  return 0;
}
