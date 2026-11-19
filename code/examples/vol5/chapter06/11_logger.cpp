#include <chrono>
#include <cstdio>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

std::mutex log_mu;

static long long now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

static void log_line(const std::string& event) {
  std::ostringstream line;
  line << '[' << std::setw(18) << now_ns() << " ns] [thread "
       << std::this_thread::get_id() << "] " << event << '\n';
  std::string s = line.str();
  std::lock_guard<std::mutex> lk(log_mu);
  std::fwrite(s.data(), 1, s.size(), stdout);
}

int main() {
  std::thread t1([] {
    log_line("t1 begin");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    log_line("t1 halfway");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    log_line("t1 done");
  });
  std::thread t2([] {
    log_line("t2 begin");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    log_line("t2 halfway");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    log_line("t2 done");
  });
  t1.join();
  t2.join();
  log_line("main: both workers joined");
  return 0;
}
