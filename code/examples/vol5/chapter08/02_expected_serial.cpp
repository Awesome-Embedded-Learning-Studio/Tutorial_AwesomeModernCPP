// expected_serial.cpp —— 串行对照程序：同批编号逐个算一遍，失败只计数，不进总数
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

// compute(i) —— 考卷给定的任务内容：编号 i 成功算 i*i，13 的正倍数抛异常
long long compute(int i) {
  if (i % 13 == 0 && i > 0) throw std::runtime_error("编号 13 的倍数注定失败");
  return static_cast<long long>(i) * i;
}

// expected.cpp 的核心计算：串行把同批编号逐个算一遍，失败只计数，不进总数
int main(int argc, char** argv) {
  int n = 10000;
  if (argc > 1) n = std::atoi(argv[1]);

  long long total = 0;
  long long success = 0;
  long long failed = 0;
  for (int i = 0; i < n; ++i) {
    try {
      total += compute(i);
      ++success;
    } catch (const std::exception&) {
      ++failed;
    }
  }
  std::printf("total=%lld success=%lld failed=%lld skipped=0\n", total, success, failed);
  return 0;
}
