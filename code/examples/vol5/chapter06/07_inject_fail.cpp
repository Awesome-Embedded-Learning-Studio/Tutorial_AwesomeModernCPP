#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

std::deque<int> items;
std::mutex m;
std::condition_variable cv;
bool closed = false;
bool failed = false;

int main() {
  std::thread producer([] {
    try {
      for (int i = 1; i <= 5; ++i) {
        if (i == 3) throw std::runtime_error("device busy while packing item 3");
        {
          std::lock_guard lk(m);
          items.push_back(i);
        }
        cv.notify_one();
      }
    } catch (...) {
      std::lock_guard lk(m);
      failed = true;
      cv.notify_all();
    }
  });
  std::thread consumer([] {
    int got = 0;
    for (;;) {
      std::unique_lock lk(m);
      cv.wait(lk, [] { return !items.empty() || failed; });
      if (!items.empty()) {
        items.pop_front();
        ++got;
        continue;
      }
      std::printf("consumer received %d items, then the failure notice\n", got);
      break;
    }
  });
  producer.join();
  consumer.join();
  return 0;
}
