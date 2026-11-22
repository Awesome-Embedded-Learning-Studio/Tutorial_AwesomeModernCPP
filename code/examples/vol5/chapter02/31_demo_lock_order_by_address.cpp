// demo_lock_order_by_address.cpp —— 没有编号的时候，按地址排（std::less）
#include <cstdio>
#include <mutex>
#include <thread>

struct Wallet {
  std::mutex m;
  long long balance;
};

void move(Wallet& from, Wallet& to, long long amount) {
  Wallet& low  = std::less<Wallet*>{}(&from, &to) ? from : to;
  Wallet& high = std::less<Wallet*>{}(&from, &to) ? to : from;
  std::lock_guard<std::mutex> a(low.m);
  std::lock_guard<std::mutex> b(high.m);
  from.balance -= amount;
  to.balance += amount;
}

int main() {
  Wallet a;
  Wallet b;
  a.balance = 700;
  b.balance = 300;
  std::thread t1([&] { for (int i = 0; i < 200000; ++i) move(a, b, 1); });
  std::thread t2([&] { for (int i = 0; i < 200000; ++i) move(b, a, 1); });
  t1.join();
  t2.join();
  std::printf("by address: a = %lld, b = %lld, sum = %lld\n",
              a.balance, b.balance, a.balance + b.balance);
  return 0;
}
