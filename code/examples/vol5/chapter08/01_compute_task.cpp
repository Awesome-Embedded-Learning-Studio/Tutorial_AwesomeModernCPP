// compute_task.cpp —— 考卷给定的任务内容：编号 i 成功算 i*i，13 的正倍数抛异常
#include <cstdio>
#include <stdexcept>

// compute(i) —— 考卷给定的任务内容：编号 i 成功算 i*i，13 的正倍数抛异常
long long compute(int i) {
  if (i % 13 == 0 && i > 0) throw std::runtime_error("编号 13 的倍数注定失败");
  return static_cast<long long>(i) * i;
}

int main() {
  // 成功的样例
  for (int i : {0, 5, 12, 100}) {
    std::printf("compute(%d) = %lld\n", i, compute(i));
  }
  // 注定失败的样例：13 的正倍数
  for (int i : {13, 26, 3900}) {
    try {
      compute(i);
    } catch (const std::exception& e) {
      std::printf("compute(%d) throws: %s\n", i, e.what());
    }
  }
  return 0;
}
