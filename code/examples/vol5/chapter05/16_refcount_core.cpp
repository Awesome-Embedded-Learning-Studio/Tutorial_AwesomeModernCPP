// refcount_core.cpp —— 引用计数两个成员的内存序搭配
// （正文只留两个成员函数，这里补一个 main 展示行为；完整的侵入式指针见素材文）
#include <atomic>
#include <cstdio>

struct Counted {
  std::atomic<int> refs{1};

  void add_ref() {
    refs.fetch_add(1, std::memory_order_relaxed);   // 只要动作完整
  }

  bool release_ref() {
    // 减到零的线程要销毁对象。读的一头接住别的线程的使用（acquire），
    // 写的一头把自己的使用压在减法之前（release）。
    return refs.fetch_sub(1, std::memory_order_acq_rel) == 1;
  }
};

int main() {
  Counted c;                        // refs 从 1 起步
  c.add_ref();                      // 拿一个新引用：1 -> 2
  c.add_ref();                      // 再拿一个：2 -> 3
  std::printf("两次 add_ref 后 refs=%d\n", c.refs.load());

  while (!c.release_ref()) {}       // 逐个放手，减到零的那一次返回 true，销毁权归它
  std::printf("减到零的那一次拿到销毁权，refs=%d\n", c.refs.load());
  return 0;
}
