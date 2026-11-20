// demo_shared.cpp —— 一份配置，三条线程都要：shared_future
#include <chrono>
#include <cstdio>
#include <future>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

std::string load_config() {
  std::this_thread::sleep_for(150ms);   // 顶替一次真实的读取
  return "port = 8080";
}

int main() {
  std::future<std::string> one_shot = std::async(load_config);
  std::shared_future<std::string> config = one_shot.share();
  std::printf("main: share 之后，future 的 valid() = %d\n",
              one_shot.valid() ? 1 : 0);

  std::vector<std::thread> readers;
  for (int i = 0; i < 3; ++i) {
    readers.emplace_back([config, i] {          // 按值拷进线程，一人一份
      const std::string& line = config.get();   // 各自的拷贝，各自 get
      std::printf("reader %d: %s\n", i, line.c_str());
    });
  }
  for (auto& t : readers) t.join();

  std::printf("main: 第一回 get 到 %s\n", config.get().c_str());
  std::printf("main: 第二回 get 到 %s\n", config.get().c_str());
  return 0;
}
