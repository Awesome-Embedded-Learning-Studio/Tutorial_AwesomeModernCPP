// demo_init.cpp —— C++20 起默认构造把原子值初始化
#include <atomic>
#include <cstdio>

int main() {
  std::atomic<int> zero;
  std::atomic_flag flag;
  std::printf("atomic<int> 默认构造 load: %d\n", zero.load());
  std::printf("atomic_flag 默认构造 test: %d\n", flag.test() ? 1 : 0);
  return 0;
}
