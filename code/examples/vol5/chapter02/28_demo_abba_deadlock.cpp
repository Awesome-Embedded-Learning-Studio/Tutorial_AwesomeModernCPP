// demo_abba_deadlock.cpp —— 方向相反的两次挪钱：各持一把，等对方松手
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

struct Wallet {
  Wallet(int number, long long b) : id(number), balance(b) {}
  std::mutex m;
  int id;
  long long balance;
};

// 按 from 然后 to 的次序拿锁。方向反过来，锁的次序也跟着反过来。
void pay(Wallet& from, Wallet& to, long long amount, const char* name) {
  std::lock_guard<std::mutex> first(from.m);
  std::printf("%s: holds the first wallet, asking for the second\n", name);
  std::fflush(stdout);
  std::this_thread::sleep_for(100ms);  // 把两条线程都停在"手里一把"的位置
  std::lock_guard<std::mutex> second(to.m);
  from.balance -= amount;
  to.balance += amount;
}

int main() {
  Wallet a(1, 700);
  Wallet b(2, 300);
  std::thread t1(pay, std::ref(a), std::ref(b), 10LL, "t1");
  std::thread t2(pay, std::ref(b), std::ref(a), 10LL, "t2");
  t1.join();
  t2.join();
  std::printf("a = %lld, b = %lld\n", a.balance, b.balance);
}
