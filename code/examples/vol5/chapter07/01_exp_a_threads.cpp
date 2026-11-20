// exp_a_threads.cpp —— 一千条"一连接一线程"的线程：量地址空间、物理内存与线程数
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

// 从 /proc/self/status 抄一行出来，VmSize 与 VmRSS 单位是 kB
static bool status_line(const char* prefix, char* out, size_t n) {
  FILE* f = std::fopen("/proc/self/status", "r");
  if (!f) return false;
  bool found = false;
  char line[256];
  while (std::fgets(line, sizeof line, f)) {
    if (std::strncmp(line, prefix, std::strlen(prefix)) == 0) {
      std::snprintf(out, n, "%s", line);
      found = true;
      break;
    }
  }
  std::fclose(f);
  return found;
}

static void report(const char* tag) {
  char line[256];
  if (status_line("Threads:", line, sizeof line))
    std::printf("%s %s", tag, line);
  if (status_line("VmSize:", line, sizeof line))
    std::printf("%s %s", tag, line);
  if (status_line("VmRSS:", line, sizeof line))
    std::printf("%s %s", tag, line);
}

int main() {
  constexpr int kN = 1000;
  std::printf("== before ==\n");
  report("");

  std::atomic<int> started{0};
  std::vector<std::thread> pool;
  pool.reserve(kN);
  for (int i = 0; i < kN; ++i) {
    pool.emplace_back([&started] {
      ++started;
      // 睡着等事件，模拟一条正在等 I/O 的连接线程
      std::this_thread::sleep_for(std::chrono::seconds(3));
    });
  }
  while (started.load() < kN)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

  std::printf("== %d threads parked ==\n", kN);
  report("");

  for (auto& t : pool) t.join();
  return 0;
}
