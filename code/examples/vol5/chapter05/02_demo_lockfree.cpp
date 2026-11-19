// demo_lockfree.cpp —— is_lock_free 与 is_always_lock_free
#include <atomic>
#include <cstdio>

struct Wide {                       // 16 个字节
  long long a;
  long long b;
};

int main() {
  std::atomic<int> ai{};
  std::atomic<long long> all{};
  std::atomic<Wide> aw{};

  std::printf("atomic<int>        is_always_lock_free=%d  is_lock_free=%d\n",
              (int)std::atomic<int>::is_always_lock_free, (int)ai.is_lock_free());
  std::printf("atomic<long long>  is_always_lock_free=%d  is_lock_free=%d\n",
              (int)std::atomic<long long>::is_always_lock_free, (int)all.is_lock_free());
  std::printf("atomic<Wide>(16B)  is_always_lock_free=%d  is_lock_free=%d\n",
              (int)std::atomic<Wide>::is_always_lock_free, (int)aw.is_lock_free());
  return 0;
}
