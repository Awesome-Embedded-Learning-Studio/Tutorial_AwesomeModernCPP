// probe_shared_from_async.cpp —— shared_future 直接从 async 的返回值构造
#include <chrono>
#include <cstdio>
#include <future>
#include <string>

using namespace std::chrono_literals;

std::string load_config() {
  std::this_thread::sleep_for(150ms);   // 顶替一次真实的读取
  return "port = 8080";
}

int main() {
  // 不经 share()，从 async 返回的 future 右值直接转换构造
  std::shared_future<std::string> sf = std::async(load_config);
  std::printf("main: 第一回 get 到 %s\n", sf.get().c_str());
  std::printf("main: 第二回 get 到 %s\n", sf.get().c_str());
  return 0;
}
