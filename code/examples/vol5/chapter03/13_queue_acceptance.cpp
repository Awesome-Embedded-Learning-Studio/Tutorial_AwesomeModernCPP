// queue_acceptance.cpp —— 多生产者多消费者的验收：件数对得上、没有重复、校验和一致
// 队列类是 3.2 全文搭完的最终形状，原实验放在头文件 queue.hpp 里，这里一并内联
#include <atomic>
#include <cstdio>
#include <memory>
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

constexpr int kProducers = 4;
constexpr int kConsumers = 4;
constexpr int kPerProducer = 2500;
constexpr int kTotal = kProducers * kPerProducer;

int main() {
  BoundedQueue<int> q(8);
  std::atomic<long long> pushed{0};
  std::atomic<long long> popped{0};
  std::atomic<long long> sum{0};
  std::atomic<long long> duplicates{0};
  std::unique_ptr<std::atomic<uint8_t>[]> seen(new std::atomic<uint8_t>[kTotal]);
  for (int i = 0; i < kTotal; ++i) seen[i].store(0, std::memory_order_relaxed);

  std::vector<std::thread> threads;
  for (int i = 0; i < kConsumers; ++i) {
    threads.emplace_back([&, i] {
      for (int k = 0; k < kTotal / kConsumers; ++k) {
        int v = -1;
        if (q.pop(v) != QueueStatus::kOk) {
          std::printf("consumer %d: 提前收到 kClosed!\n", i);
          return;
        }
        if (seen[v].exchange(1, std::memory_order_relaxed)) {
          duplicates.fetch_add(1, std::memory_order_relaxed);
        }
        sum.fetch_add(v, std::memory_order_relaxed);
        popped.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (int i = 0; i < kProducers; ++i) {
    threads.emplace_back([&, i] {
      for (int k = 0; k < kPerProducer; ++k) {
        int v = i * kPerProducer + k;  // 0..9999，每件恰好出现一次
        if (q.push(v) == QueueStatus::kOk) {
          pushed.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto& t : threads) t.join();

  long long expect_sum = static_cast<long long>(kTotal - 1) * kTotal / 2;
  std::printf("总入队 %lld  总出队 %lld\n", pushed.load(), popped.load());
  std::printf("重复取到 %lld 件  校验和 %lld（应为 %lld，%s）\n",
              duplicates.load(), sum.load(), expect_sum,
              sum.load() == expect_sum ? "一致" : "不一致");
  std::printf("收尾时 size = %zu\n", q.size());
  return 0;
}
