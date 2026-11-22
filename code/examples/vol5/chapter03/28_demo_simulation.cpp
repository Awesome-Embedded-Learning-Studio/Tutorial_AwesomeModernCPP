// demo_simulation.cpp —— mutex + 条件变量模拟三个工具，各跑一遍验收
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <syncstream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

class LatchSim {
public:
  explicit LatchSim(std::ptrdiff_t count) : count_(count) {}
  void count_down(std::ptrdiff_t update = 1) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      count_ -= update;
    }
    if (count_ <= 0) cv_.notify_all();   // 归零才叫，锁外叫醒
  }
  void wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return count_ <= 0; });
  }
private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::ptrdiff_t count_;
};

class BarrierSim {
public:
  explicit BarrierSim(std::ptrdiff_t expected)
      : initial_(expected), count_(expected) {}
  void arrive_and_wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    std::size_t gen = generation_;       // 锁内记住自己属于哪一代
    if (--count_ == 0) {
      ++generation_;
      count_ = initial_;
      lock.unlock();
      cv_.notify_all();
    } else {
      cv_.wait(lock, [&] { return gen != generation_; });
    }
  }
private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::ptrdiff_t initial_;
  std::ptrdiff_t count_;
  std::size_t generation_ = 0;
};

class CountingSemaphoreSim {
public:
  explicit CountingSemaphoreSim(std::ptrdiff_t desired) : counter_(desired) {}
  void acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return counter_ > 0; });
    --counter_;
  }
  void release(std::ptrdiff_t update = 1) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      counter_ += update;
    }
    cv_.notify_all();
  }
private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::ptrdiff_t counter_;
};

int main() {
  {   // LatchSim：三件初始化活，一个等待方
    LatchSim ready(3);
    std::vector<std::thread> inits;
    for (int i = 0; i < 3; ++i) {
      inits.emplace_back([&ready, i] {
        std::this_thread::sleep_for(10ms * (3 - i));
        std::osyncstream(std::cout) << "[latch sim] 活 " << i << " 完成\n";
        ready.count_down();
      });
    }
    ready.wait();
    std::printf("[latch sim] 三件活齐了，往下走\n");
    for (auto& t : inits) t.join();
  }

  {   // BarrierSim：两条线程两轮齐步
    BarrierSim sync(2);
    std::vector<std::thread> workers;
    for (int i = 0; i < 2; ++i) {
      workers.emplace_back([&sync, i] {
        for (int p = 1; p <= 2; ++p) {
          std::this_thread::sleep_for(10ms * (p + i));
          sync.arrive_and_wait();
          std::osyncstream(std::cout)
              << "[barrier sim] worker " << i << " 第 " << p << " 轮过线\n";
        }
      });
    }
    for (auto& t : workers) t.join();
    std::printf("[barrier sim] 两轮走完\n");
  }

  {   // CountingSemaphoreSim：8 抢 3，量峰值
    CountingSemaphoreSim slots(3);
    std::atomic<int> inside{0};
    std::atomic<int> peak{0};
    std::vector<std::thread> tasks;
    for (int i = 0; i < 8; ++i) {
      tasks.emplace_back([&] {
        slots.acquire();
        int now = ++inside;
        int old = peak.load();
        while (now > old && !peak.compare_exchange_weak(old, now)) {}
        std::this_thread::sleep_for(10ms);
        --inside;
        slots.release();
      });
    }
    for (auto& t : tasks) t.join();
    std::printf("[semaphore sim] 8 个任务做完，峰值 %d（上限 3）\n", peak.load());
  }
  return 0;
}
