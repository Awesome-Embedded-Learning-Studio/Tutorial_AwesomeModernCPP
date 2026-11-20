// queue_static_assert.cpp —— 两道编译期的闸：移动构造、移动赋值会抛的类型，实例化时就拦下
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <queue>
#include <type_traits>
#include <utility>

enum class QueueStatus {
  kOk,
  kClosed,
};

template <typename T>
class BoundedQueue {
  static_assert(std::is_nothrow_move_constructible_v<T>,
                "T must be nothrow move constructible");
  static_assert(std::is_nothrow_move_assignable_v<T>,
                "T must be nothrow move assignable");

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
  BoundedQueue<int> q(4);   // int 移动构造、移动赋值都不抛，两道闸都过得去
  q.push(7);
  int v = 0;
  q.pop(v);
  std::printf("int 过闸：push 7，pop -> %d\n", v);
  return 0;
}
