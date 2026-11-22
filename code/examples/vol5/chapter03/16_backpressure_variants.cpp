// backpressure_variants.cpp —— 队列满了的两条出路：try_push 立即拒绝，push_replace_oldest 挤掉最老的
#include <condition_variable>
#include <cstddef>
#include <cstdio>
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

  bool try_push(T value) {   // 拒绝：满了立即返回，决定权交给调用者
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_ || queue_.size() >= capacity_) return false;
      queue_.push(std::move(value));
    }
    not_empty_.notify_one();
    return true;
  }

  bool push_replace_oldest(T value, bool& evicted) {  // 覆盖：挤掉最老的，新来的必进
    evicted = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) return false;
      if (queue_.size() >= capacity_) {
        queue_.pop();          // 挤掉队头最老的一件
        evicted = true;
      }
      queue_.push(std::move(value));
    }
    not_empty_.notify_one();
    return true;
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
  for (int i = 0; i < 4; ++i) q.push(i);   // 装满

  bool ok = q.try_push(99);
  std::printf("满了 try_push(99) -> %s\n", ok ? "true" : "false");

  bool evicted = false;
  q.push_replace_oldest(100, evicted);     // 挤掉队头的 0
  std::printf("满了 push_replace_oldest(100)，挤掉最老的 -> %s\n",
              evicted ? "true" : "false");

  int v = 0;
  while (q.pop(v) == QueueStatus::kOk) {
    std::printf("pop -> %d\n", v);
  }
  return 0;
}
