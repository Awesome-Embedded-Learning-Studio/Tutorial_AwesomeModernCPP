// queue_close_experiment.cpp —— 关闭实验：消费者睡在空队列上，close 的 notify_all 把它们叫醒去 drain
// 队列类是 3.2 全文搭完的最终形状，原实验放在头文件 queue.hpp 里，这里一并内联
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>
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

constexpr int kPerProducer = 100;
constexpr int kProducers = 2;

int main() {
  BoundedQueue<int> q(4);
  std::atomic<int> total{0};

  std::vector<std::thread> consumers;
  std::vector<int> consumed(3, 0);
  std::vector<int> exit_way(3, -1);
  for (int i = 0; i < 3; ++i) {
    consumers.emplace_back([&, i] {
      while (true) {
        int v = 0;
        QueueStatus st = q.pop(v);
        if (st == QueueStatus::kClosed) {
          exit_way[i] = 0;
          break;
        }
        ++consumed[i];
        total.fetch_add(1);
      }
    });
  }

  std::vector<std::thread> producers;
  for (int i = 0; i < kProducers; ++i) {
    producers.emplace_back([&] {
      for (int k = 0; k < kPerProducer; ++k) q.push(k);
    });
  }
  for (auto& t : producers) t.join();
  q.close();
  for (auto& t : consumers) t.join();

  std::printf("总入队 %d  总出队 %d（应为 %d）\n", kPerProducer * kProducers,
              total.load(), kPerProducer * kProducers);
  for (int i = 0; i < 3; ++i) {
    std::printf("consumer %d: 消费 %3d 件，以 %s 退出\n", i, consumed[i],
                exit_way[i] == 0 ? "kClosed" : "???");
  }
  std::printf("关停后 size = %zu\n", q.size());

  QueueStatus st = q.push(1);
  std::printf("关闭后再 push -> %s\n",
              st == QueueStatus::kClosed ? "kClosed" : "kOk");
  int v = 0;
  st = q.pop(v);
  std::printf("关闭且空了再 pop -> %s\n",
              st == QueueStatus::kClosed ? "kClosed" : "kOk");
  return 0;
}
