#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

std::deque<int> items;
std::mutex m;
std::condition_variable_any cv_any;
bool closed = false;

static std::string exit_reason(bool stop_seen) {
  if (stop_seen) return "stop requested";
  if (closed) return "closed";
  return "items left";
}

int main() {
  std::jthread b([](std::stop_token st) {
    std::unique_lock lk(m);
    cv_any.wait(lk, st, [&] { return !items.empty() || closed; });
    std::printf("consumer B exits on: %s\n",
                exit_reason(st.stop_requested()).c_str());
  });
  std::jthread a([](std::stop_token st) {
    std::unique_lock lk(m);
    cv_any.wait(lk, st, [&] { return !items.empty() || closed; });
    std::printf("consumer A exits on: %s\n",
                exit_reason(st.stop_requested()).c_str());
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto t0 = std::chrono::steady_clock::now();
  b.request_stop();
  b.join();
  auto ms_b = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
  std::printf("request_stop() to B exit: %lld ms\n", (long long)ms_b);

  auto t1 = std::chrono::steady_clock::now();
  {
    std::lock_guard lk(m);
    closed = true;
  }
  cv_any.notify_all();
  a.join();
  auto ms_a = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t1).count();
  std::printf("close() to A exit: %lld ms\n", (long long)ms_a);
  return 0;
}
