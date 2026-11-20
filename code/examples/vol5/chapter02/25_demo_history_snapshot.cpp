// demo_history_snapshot.cpp —— 锁里复制一份，锁外随便用
#include <cstdio>
#include <mutex>
#include <vector>

class History {
 public:
  void add(int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_.push_back(v);
  }
  std::vector<int> snapshot() const {
    std::lock_guard<std::mutex> lock(m_);
    return items_;
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  for (int v : {1, 2, 3}) history.add(v);
  std::vector<int> copy = history.snapshot();
  history.add(4);              // 快照之后容器再动，副本不受影响
  long long sum = 0;
  for (int v : copy) sum += v;
  std::printf("snapshot holds %zu items summing to %lld\n", copy.size(), sum);
  return 0;
}
