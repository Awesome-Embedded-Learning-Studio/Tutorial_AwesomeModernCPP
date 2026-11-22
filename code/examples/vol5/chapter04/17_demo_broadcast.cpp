// demo_broadcast.cpp —— 一处供值，多处取用：promise 配 shared_future
#include <chrono>
#include <cstdio>
#include <future>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

int main() {
  std::promise<std::string> ready;
  std::shared_future<std::string> config = ready.get_future().share();

  std::vector<std::thread> pool;
  for (int i = 0; i < 3; ++i) {
    pool.emplace_back([config, i] {            // 每条线程一份自己的拷贝
      const std::string& line = config.get();  // 没就绪就等，就绪了一起醒
      std::printf("worker %d: 读到 %s\n", i, line.c_str());
    });
  }

  std::this_thread::sleep_for(150ms);   // 顶替一段准备工作
  ready.set_value("pool_size = 4");     // 供值端在 main 手里，什么时候给由咱们定
  for (auto& t : pool) t.join();
  return 0;
}
