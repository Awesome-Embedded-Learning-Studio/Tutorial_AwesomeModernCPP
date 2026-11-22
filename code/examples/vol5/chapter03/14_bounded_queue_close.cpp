// bounded_queue_close.cpp —— 带关闭语义的队列类：close 之后 push 拒收，pop 先取干净再报 kClosed
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>
#include <cstdio>

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

int main() {
  BoundedQueue<int> q(4);
  q.push(1);
  q.push(2);
  q.close();   // 生产者收工，收盘

  std::printf("关闭后再 push -> %s\n",
              q.push(3) == QueueStatus::kClosed ? "kClosed" : "kOk");
  int v = 0;
  while (q.pop(v) == QueueStatus::kOk) {
    std::printf("pop -> %d\n", v);   // drain：把剩下的货取干净
  }
  std::printf("取空且已关，再 pop -> %s，size = %zu\n",
              q.pop(v) == QueueStatus::kClosed ? "kClosed" : "kOk", q.size());
  return 0;
}
