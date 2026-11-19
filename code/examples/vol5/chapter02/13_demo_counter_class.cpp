// demo_counter_class.cpp —— 锁和数据住进同一个类，外面只留操作
#include <cstdio>
#include <mutex>

class Counter {
public:
  void add(int n) {
    std::lock_guard<std::mutex> lock(m_);
    value_ += n;
  }

  long long value() const {
    std::lock_guard<std::mutex> lock(m_);
    return value_;
  }

private:
  mutable std::mutex m_;
  long long value_ = 0;
};

int main() {
  Counter c;
  for (int i = 0; i < 1000; ++i) c.add(3);
  std::printf("after 1000 adds of 3: %lld\n", c.value());
  return 0;
}
