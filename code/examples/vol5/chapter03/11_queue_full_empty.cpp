// queue_full_empty.cpp —— 队列空了 pop 睡、队列满了 push 睡：容量 2，消费每件歇 100 ms
// 队列类是 3.2 全文搭完的最终形状，原实验放在头文件 queue.hpp 里，这里一并内联
#include <chrono>
#include <cstdio>
#include <thread>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>

enum class QueueStatus {
  kOk,
  kClosed,
};

template <typename T>
class BoundedQueue {
public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

  QueueStatus push(T value) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_full_.wait(lock, [this] { return queue_.size() < capacity_ || closed_; });
    if (closed_) return QueueStatus::kClosed;   // 关了，货不收
    queue_.push(std::move(value));
    not_empty_.notify_one();
    return QueueStatus::kOk;
  }

  QueueStatus pop(T& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
    if (queue_.empty()) return QueueStatus::kClosed;  // 关了且取空了
    out = std::move(queue_.front());
    queue_.pop();
    not_full_.notify_one();
    return QueueStatus::kOk;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    not_full_.notify_all();
    not_empty_.notify_all();
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

private:
  std::queue<T> queue_;
  std::size_t capacity_;
  bool closed_ = false;
  mutable std::mutex mutex_;
  std::condition_variable not_full_;
  std::condition_variable not_empty_;
};

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  BoundedQueue<int> q(2);

  std::printf("--- 满的方向：容量 2，消费每件歇 100 ms ---\n");
  std::thread consumer([&q] {
    for (int i = 0; i < 4; ++i) {
      int v = 0;
      q.pop(v);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));  // 让消费者进 pop 等着
  for (int i = 0; i < 4; ++i) {
    auto t0 = Clock::now();
    q.push(i);
    std::printf("push #%d 用时 %6.1f ms\n", i, ms_since(t0));
  }
  consumer.join();

  std::printf("--- 空的方向：队列空着，生产者 80 ms 后才来一件 ---\n");
  std::thread producer([&q] {
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    q.push(99);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  auto t0 = Clock::now();
  int v = 0;
  q.pop(v);
  std::printf("pop 用时 %6.1f ms，拿到 %d\n", ms_since(t0), v);
  producer.join();
  return 0;
}
