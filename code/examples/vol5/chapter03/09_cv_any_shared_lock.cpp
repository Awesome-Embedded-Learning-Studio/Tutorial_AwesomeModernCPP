// cv_any_shared_lock.cpp —— condition_variable_any 配 shared_lock：三条读线程等一份配置发布
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

std::shared_mutex rm;
std::condition_variable_any cv;
bool published = false;
std::string config;

int main() {
  std::vector<std::thread> readers;
  for (int i = 0; i < 3; ++i) {
    readers.emplace_back([i] {
      std::shared_lock<std::shared_mutex> lk(rm);   // 共享锁，几个人同时读
      cv.wait(lk, [] { return published; });
      std::printf("reader %d: %s\n", i, config.c_str());
    });
  }
  std::this_thread::sleep_for(50ms);
  {
    std::unique_lock<std::shared_mutex> lk(rm);     // 写的时候独占
    config = "level = 3";
    published = true;
  }
  cv.notify_all();
  for (auto& t : readers) t.join();
  std::printf("main: 三条读线程读到的都是 level = 3\n");
  return 0;
}
