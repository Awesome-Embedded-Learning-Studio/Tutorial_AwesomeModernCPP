// exp_b_suspend.cpp —— 协程的暂停与恢复：谁恢复它，它就在谁的线程上跑
// 二幕：一千个挂起的协程，一条线程，量地址空间与物理内存
#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static std::string tid() {
  std::ostringstream oss;
  oss << std::this_thread::get_id();
  return oss.str();
}

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

// awaiter：挂起，把句柄登记进列表，等别人来恢复
struct Park {
  static std::vector<std::coroutine_handle<>> parked;
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> h) { parked.push_back(h); }
  void await_resume() {}
};
std::vector<std::coroutine_handle<>> Park::parked;

// 最小任务类型。零件各自干什么归 7.2，本篇只要它撑起 co_await
struct Task {
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };

  std::coroutine_handle<> h;
  explicit Task(std::coroutine_handle<> hh) : h(hh) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  Task(Task&& o) noexcept : h(o.h) { o.h = {}; }
  Task& operator=(Task&& o) noexcept {
    if (this != &o) {
      if (h) h.destroy();
      h = o.h;
      o.h = {};
    }
    return *this;
  }
  ~Task() { if (h) h.destroy(); }
};

// 处理一件"要等"的活：stage 1，挂起等事件，恢复后 stage 2
// done 非空时是安静模式，只计数不打印
Task worker(int id, std::atomic<int>* done) {
  if (done == nullptr)
    std::printf("  worker %d stage1 on thread %s, suspend\n", id, tid().c_str());
  co_await Park{};
  if (done != nullptr)
    ++*done;
  else
    std::printf("  worker %d stage2 on thread %s, finished\n", id, tid().c_str());
}

int main() {
  { // 一幕：同一个协程体分两段跑，谁恢复它，它就在谁的线程上继续
    Task t = worker(0, nullptr);
    std::printf("main: worker suspended, parked=%zu\n", Park::parked.size());
    std::thread driver([] { Park::parked.back().resume(); });
    driver.join();
    std::printf("main: worker finished\n");
  }
  Park::parked.clear();

  { // 二幕：一千个挂起的协程，全程只有主线程
    constexpr int kN = 1000;
    std::atomic<int> done{0};
    std::printf("== before %d workers ==\n", kN);
    report("");
    std::vector<Task> tasks;
    tasks.reserve(kN);
    for (int i = 0; i < kN; ++i)
      tasks.emplace_back(worker(i, &done));
    while (static_cast<int>(Park::parked.size()) < kN)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::printf("== %d coroutines suspended ==\n", kN);
    report("");
    for (auto& h : Park::parked) h.resume();
    std::printf("== resumed, done=%d ==\n", done.load());
  }
  return 0;
}
