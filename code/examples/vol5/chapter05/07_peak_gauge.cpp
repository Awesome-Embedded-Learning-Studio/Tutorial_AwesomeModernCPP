// peak_gauge.cpp —— 峰值仪表：只许涨不许跌的 CAS 循环（3.4 量峰值片段的补全版）
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

std::atomic<int> inside{0};    // 当前同时在线的任务数
std::atomic<int> peak{0};      // 峰值仪表：只许涨，不许跌

void task() {
  int now = ++inside;
  int old = peak.load();
  while (now > old && !peak.compare_exchange_weak(old, now)) {}
  std::this_thread::sleep_for(10ms);   // 在里面待一会儿，让并发叠起来
  --inside;
}

int main() {
  std::vector<std::thread> ts;
  for (int i = 0; i < 4; ++i) ts.emplace_back(task);
  for (auto& t : ts) t.join();
  std::printf("4 个任务做完，inside=%d，峰值 %d\n", inside.load(), peak.load());
  return 0;
}
