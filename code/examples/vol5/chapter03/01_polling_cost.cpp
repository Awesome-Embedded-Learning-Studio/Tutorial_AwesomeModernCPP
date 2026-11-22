// polling_cost.cpp —— 转圈的代价：空转、两种睡眠轮询、条件变量，四种等法同场量一遍
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

std::atomic<int> done{0};
std::atomic<long long> finished_at{0};
std::mutex m;
std::condition_variable cv;

long long now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             Clock::now().time_since_epoch()).count();
}

template <class Wait>
void run(const char* name, Wait wait) {
  done = 0;
  finished_at = 0;
  std::thread worker([] {
    std::this_thread::sleep_for(100ms);   // 顶替“算完”那段耗时
    finished_at = now_ns();
    { std::lock_guard<std::mutex> g(m); done = 1; }
    cv.notify_one();
  });
  std::this_thread::sleep_for(10ms);      // 让 worker 睡下，主线程再开始等
  std::clock_t c0 = std::clock();
  long long rounds = 0;
  wait(rounds);
  std::clock_t c1 = std::clock();
  long long seen = now_ns();
  worker.join();
  std::printf("%-8s rounds=%9lld  cpu=%7.2f ms  delay=%9.1f us\n", name, rounds,
              1000.0 * (c1 - c0) / CLOCKS_PER_SEC,
              (seen - finished_at.load()) / 1000.0);
}

int main() {
  for (int pass = 1; pass <= 2; ++pass) {
    std::printf("--- pass %d ---\n", pass);
    run("spin", [](long long& r) {
      while (done.load(std::memory_order_relaxed) == 0) ++r;
    });
    run("sleep1", [](long long& r) {
      while (done.load() == 0) { ++r; std::this_thread::sleep_for(1ms); }
    });
    run("sleep20", [](long long& r) {
      while (done.load() == 0) { ++r; std::this_thread::sleep_for(20ms); }
    });
    run("cv", [](long long&) {
      std::unique_lock<std::mutex> lk(m);
      cv.wait(lk, [] { return done.load() == 1; });
    });
  }
  return 0;
}
