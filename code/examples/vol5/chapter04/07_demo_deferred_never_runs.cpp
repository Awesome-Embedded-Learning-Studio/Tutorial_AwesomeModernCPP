// demo_deferred_never_runs.cpp —— deferred 不调用就永不执行，析构也不补跑
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

double since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void bake() {
  std::this_thread::sleep_for(200ms);
  std::printf("  bake: 面包出炉了\n");
}

int main() {
  {
    auto t0 = Clock::now();
    std::future<void> f = std::async(std::launch::deferred, bake);
    std::printf("[deferred] future 一直在手里，没调 get 也没调 wait\n");
    std::printf("[deferred] 到作用域结尾丢弃，共 %.1f ms，上面没有 bake 的输出\n",
                since(t0));
  }
  {
    auto t0 = Clock::now();
    std::async(std::launch::async, bake);   // 临时 future 在分号处析构
    std::printf("[async] 丢弃临时 future，整个表达式 %.1f ms\n", since(t0));
  }
  std::printf("main 到头了，bake 只烤成过一次\n");
  return 0;
}
