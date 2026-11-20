// bounded_queue_teaching.cpp —— 教学版有容量队列：空了消费者等，满了生产者等，各配一把条件变量
#include <condition_variable>
#include <mutex>
#include <queue>
#include <cstddef>
#include <utility>
#include <cstdio>
#include <thread>

template <typename T>
class BoundedQueue {
public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

  void push(T value) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_full_.wait(lock, [this] { return queue_.size() < capacity_; });
    queue_.push(std::move(value));
    not_empty_.notify_one();
  }

  T pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this] { return !queue_.empty(); });
    T value = std::move(queue_.front());
    queue_.pop();
    not_full_.notify_one();
    return value;
  }

private:
  std::queue<T> queue_;
  std::size_t capacity_;
  std::mutex mutex_;
  std::condition_variable not_full_;   // 等有位的，是生产者
  std::condition_variable not_empty_;  // 等有货的，是消费者
};

int main() {
  BoundedQueue<int> q(2);   // 容量 2，放 3 件，生产者会等一回位置

  std::thread consumer([&q] {
    for (int i = 0; i < 3; ++i) {
      std::printf("pop -> %d\n", q.pop());
    }
  });
  for (int i = 0; i < 3; ++i) {
    q.push(i);
  }
  consumer.join();
  return 0;
}
