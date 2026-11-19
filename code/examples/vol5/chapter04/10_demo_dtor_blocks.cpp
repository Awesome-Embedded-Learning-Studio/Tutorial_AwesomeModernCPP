// demo_dtor_blocks.cpp —— 丢弃式 async 逐个等待，启动完再收集才并行
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

double since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void nap(const char* tag) {
  std::this_thread::sleep_for(300ms);
  std::printf("  %s 干完了\n", tag);
}

int main() {
  auto t0 = Clock::now();
  std::async(std::launch::async, nap, "A1");
  double a1 = since(t0);
  std::async(std::launch::async, nap, "A2");
  double a2 = since(t0);
  std::async(std::launch::async, nap, "A3");
  std::printf("[丢弃式] A1 用 %.1f ms，A2 累计 %.1f ms，A3 累计 %.1f ms\n",
              a1, a2, since(t0));

  t0 = Clock::now();
  std::vector<std::future<void>> fs;
  for (const char* tag : {"B1", "B2", "B3"}) {
    fs.push_back(std::async(std::launch::async, nap, tag));
  }
  for (auto& f : fs) f.get();
  std::printf("[启动完再收集] 三个一起 %.1f ms\n", since(t0));

  t0 = Clock::now();
  std::async(std::launch::deferred, nap, "C");
  std::printf("[deferred 丢弃] %.1f ms，上面没有 C 干完的输出\n", since(t0));

  t0 = Clock::now();
  {
    std::promise<void> p;   // promise 是 4.3《自行传递结果》的主角，这里只借它的 future 当对照
    std::future<void> f = p.get_future();
    (void)f;
  }
  std::printf("[promise 的 future 析构] %.1f ms，谁也不等\n", since(t0));
  return 0;
}
