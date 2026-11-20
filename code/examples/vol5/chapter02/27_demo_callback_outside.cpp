// demo_callback_outside.cpp —— 锁里取副本，锁外喂给回调
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
  void for_each(F f) const {
    std::vector<int> copy;
    {
      std::lock_guard<std::mutex> lock(m_);
      copy = items_;
    }
    for (const auto& item : copy) f(item);
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  for (int v : {1, 2, 3}) history.add(v);

  long long sum = 0;
  history.for_each([&](const int& item) { sum += item; });   // 副本里的引用，随便用
  for (int i = 0; i < 100; ++i) history.add(0);              // 之后再动也不影响
  std::printf("callback summed the snapshot to %lld\n", sum);
  return 0;
}
