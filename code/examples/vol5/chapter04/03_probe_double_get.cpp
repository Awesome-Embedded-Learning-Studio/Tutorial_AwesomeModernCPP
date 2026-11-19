// probe_double_get.cpp —— get 过一次的 future，再 get 一次会怎样
#include <cstdio>
#include <future>

int answer() { return 42; }

int main() {
  std::future<int> f = std::async(answer);
  int first = f.get();
  std::printf("第一回 get = %d，valid() = %d\n", first, f.valid() ? 1 : 0);
  try {
    std::printf("第二回 get = %d\n", f.get());   // 标准层面是未定义行为
  } catch (const std::exception& e) {
    std::printf("第二回 get 抛了异常：%s\n", e.what());
  }
  return 0;
}
