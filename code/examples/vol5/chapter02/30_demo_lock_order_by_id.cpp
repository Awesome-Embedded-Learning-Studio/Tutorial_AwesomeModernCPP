// demo_lock_order_by_id.cpp —— 按编号从小到大拿锁：环画不出来
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

struct Wallet {
  std::mutex m;
  int id;
  long long balance;
};

void move(Wallet& from, Wallet& to, long long amount) {
  Wallet& low  = from.id < to.id ? from : to;
  Wallet& high = from.id < to.id ? to : from;
  std::lock_guard<std::mutex> a(low.m);
  std::lock_guard<std::mutex> b(high.m);
  from.balance -= amount;
  to.balance += amount;
}

long long read_total(Wallet& a, Wallet& b) {
  long long va, vb;
  {
    std::lock_guard<std::mutex> ga(a.m);
    va = a.balance;
  }
  std::this_thread::sleep_for(50us);   // 放下 A 之后、拿起 B 之前：挪动照常发生
  {
    std::lock_guard<std::mutex> gb(b.m);
    vb = b.balance;
  }
  return va + vb;
}

int main() {
  // 第一回：只有两条对向挪动的线程，200000 次来回
  {
    Wallet a;
    Wallet b;
    a.id = 1; a.balance = 700;
    b.id = 2; b.balance = 300;
    std::thread t1([&] { for (int i = 0; i < 200000; ++i) move(a, b, 1); });
    std::thread t2([&] { for (int i = 0; i < 200000; ++i) move(b, a, 1); });
    t1.join();
    t2.join();
    std::printf("by id: a = %lld, b = %lld, sum = %lld\n",
                a.balance, b.balance, a.balance + b.balance);
  }
  // 第二回：另开一条观察线程，一把一把地读两个钱包
  {
    Wallet a;
    Wallet b;
    a.id = 1; a.balance = 700;
    b.id = 2; b.balance = 300;
    std::thread t1([&] { for (int i = 0; i < 200000; ++i) move(a, b, 1); });
    std::thread t2([&] { for (int i = 0; i < 200000; ++i) move(b, a, 1); });
    long long sampled = 0, bad = 0;
    int printed = 0;
    std::thread observer([&] {
      while (sampled < 50000) {
        long long t = read_total(a, b);
        ++sampled;
        if (t != 1000) {
          ++bad;
          if (printed < 3) {
            std::printf("sample %lld: total %lld\n", sampled, t);
            ++printed;
          }
        }
      }
    });
    t1.join();
    t2.join();
    observer.join();
    std::printf("sampled %lld totals, %lld were not 1000\n", sampled, bad);
    std::printf("after join: a = %lld, b = %lld, sum = %lld\n",
                a.balance, b.balance, a.balance + b.balance);
  }
  return 0;
}
