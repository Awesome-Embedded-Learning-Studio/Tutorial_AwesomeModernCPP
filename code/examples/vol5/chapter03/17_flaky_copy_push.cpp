// flaky_copy_push.cpp —— 拷贝构造从第 3 次起必抛的类型：第 3 件左值抛在进 push 函数体之前
// 队列类是 3.2 全文搭完的最终形状，原实验放在头文件 queue.hpp 里，这里一并内联
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <queue>
#include <stdexcept>
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

// 拷贝构造从第 3 次起必抛，移动操作不抛
struct FlakyCopy {
  int v;
  inline static int copies = 0;
  explicit FlakyCopy(int x) : v(x) {}
  FlakyCopy(FlakyCopy&&) noexcept = default;
  FlakyCopy& operator=(FlakyCopy&&) noexcept = default;
  FlakyCopy(const FlakyCopy& o) : v(o.v) {
    if (++copies >= 3) throw std::runtime_error("copy #3 爆了");
  }
};

int main() {
  BoundedQueue<FlakyCopy> q(8);
  FlakyCopy a{1}, b{2}, c{3};
  q.push(a);            // 第 1 次拷贝，花在进参数上
  q.push(b);            // 第 2 次
  try {
    q.push(c);          // 第 3 次，抛在进函数体之前
  } catch (const std::runtime_error& e) {
    std::printf("push(c) 抛出：%s\n", e.what());
  }
  std::printf("队列里 %zu 件（应为 2）\n", q.size());
  q.push(FlakyCopy{4}); // 右值：一路移动，不碰拷贝构造
  std::printf("再 push 一件右值，队列里 %zu 件\n", q.size());

  q.close();   // 收工，让下面的排空循环能取完退出
  FlakyCopy out{0};
  while (q.pop(out) == QueueStatus::kOk) {
    std::printf("pop -> %d\n", out.v);
  }
  std::printf("收尾 size = %zu\n", q.size());
  return 0;
}
