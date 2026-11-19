// demo_callback_in_lock.cpp —— 回调在锁里跑：引用存下来就漏出去了
#include <cstdio>
#include <mutex>
#include <vector>

class History {
 public:
  void add(int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_.push_back(v);
  }
  template <class F>
  void for_each(F f) {              // 回调在锁里跑
    std::lock_guard<std::mutex> lock(m_);
    for (auto& item : items_) f(item);
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  for (int v : {1, 2, 3}) history.add(v);

  std::vector<int*> kept;           // 回调把内部引用的位置存了下来
  history.for_each([&](int& item) { kept.push_back(&item); });

  for (int i = 0; i < 100; ++i) history.add(0);   // 重新分配后位置全失效
  std::printf("callback kept %zu positions, first one now reads %d\n",
              kept.size(), *kept.front());
  return 0;
}
