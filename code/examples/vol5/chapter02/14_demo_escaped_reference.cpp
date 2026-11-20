// demo_escaped_reference.cpp —— ref() 加了锁，交出去的引用不归锁管
#include <cstdio>
#include <mutex>

class Box {
public:
  int& ref() {
    std::lock_guard<std::mutex> lock(m_);
    return value_;
  }
  void set(int v) {
    std::lock_guard<std::mutex> lock(m_);
    value_ = v;
  }

private:
  std::mutex m_;
  int value_ = 0;
};

int main() {
  Box box;
  int& r = box.ref();
  box.set(42);
  std::printf("through the escaped reference: %d\n", r);
}
