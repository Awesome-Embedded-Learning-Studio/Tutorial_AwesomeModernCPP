// demo_cas.cpp —— CAS 的两条路：expected 被覆盖，失败之后重算重试
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace std::chrono_literals;

std::atomic<int> quota{10};      // 咱们想往当前值上再叠加 5，前提是没人动过

void meddler() {                 // 半路把值改掉的人
  std::this_thread::sleep_for(50ms);
  quota.store(100);
}

int main() {
  // 第一步：值对不上，CAS 返回 false，顺带把当前值写回 expected
  std::atomic<int> v{10};
  int expected = 99;
  bool ok = v.compare_exchange_strong(expected, 20);
  std::printf("expected=99 去 CAS：ok=%d，v 还是 %d，expected 被覆盖成 %d\n",
              ok ? 1 : 0, v.load(), expected);

  // 第二步：读改写拆成 CAS 循环，中途值被改，失败一轮后重算成功
  std::thread m(meddler);
  expected = quota.load();                 // 读到 10
  std::this_thread::sleep_for(100ms);      // 故意把窗口拉开，让 meddler 赶在动手之前改值
  m.join();

  int attempts = 0;
  while (true) {
    int desired = expected + 5;
    if (quota.compare_exchange_strong(expected, desired)) {
      std::printf("第 %d 次尝试成功：%d + 5 -> %d\n", attempts + 1, expected, quota.load());
      break;
    }
    ++attempts;
    std::printf("第 %d 次尝试失败：值已经是 %d（expected 也被覆盖成它），拿它重算\n",
                attempts, expected);
  }
  return 0;
}
