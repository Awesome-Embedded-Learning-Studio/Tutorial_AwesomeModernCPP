// demo_flag.cpp —— atomic_flag 自旋锁：标准唯一保证无锁的原子类型
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic_flag busy{};      // C++20 起默认构造就是清除态；C++11 得写 ATOMIC_FLAG_INIT

void lock_flag() { while (busy.test_and_set(std::memory_order_acquire)) {} }
void unlock_flag() { busy.clear(std::memory_order_release); }

long long guarded = 0;        // 普通变量，保护全靠上面那把自旋锁

void work(int rounds) {
  for (int i = 0; i < rounds; ++i) {
    lock_flag();
    ++guarded;
    unlock_flag();
  }
}

int main() {
  std::printf("flag 默认构造后 test(): %d\n", busy.test() ? 1 : 0);
  std::thread a(work, 100000);
  std::thread b(work, 100000);
  a.join();
  b.join();
  std::printf("两条线程各加 100000 次，结果 %lld\n", guarded);
  return 0;
}
