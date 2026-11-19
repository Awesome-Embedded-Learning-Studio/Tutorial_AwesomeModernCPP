// fragile_move_pop.cpp —— 移动构造从第 2 次起必抛的类型：元素永远卡在队头，每回 pop 同一处摔倒
// NaiveQueue 是 3.2 第 1 节教学版的副本，pop 按值返回的旧签名，专门装这个类型
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <utility>

template <typename T>
class NaiveQueue {
public:
  explicit NaiveQueue(std::size_t capacity) : capacity_(capacity) {}

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

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

private:
  std::queue<T> queue_;
  std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable not_full_;
  std::condition_variable not_empty_;
};

// 移动构造从第 2 次起必抛
struct Fragile {
  int v;
  inline static int moves = 0;
  explicit Fragile(int x) : v(x) {}
  Fragile(const Fragile&) = delete;
  Fragile(Fragile&& o) : v(o.v) {
    if (++moves >= 2) throw std::runtime_error("第 2 次移动起，一搬就爆");
  }
};

int main() {
  NaiveQueue<Fragile> nq(4);
  nq.push(Fragile{7});
  std::printf("push 成功，队列里 %zu 件\n", nq.size());
  try {
    Fragile got = nq.pop();  // 从队列往外搬的那次移动
    std::printf("pop -> %d\n", got.v);
  } catch (const std::runtime_error& e) {
    std::printf("pop 抛出：%s\n", e.what());
  }
  std::printf("队列里 %zu 件（元素还卡着）\n", nq.size());
  try {
    Fragile got = nq.pop();  // 再来一回，还是那件，还是同一次摔倒
    std::printf("pop -> %d\n", got.v);
  } catch (const std::runtime_error& e) {
    std::printf("再 pop 又抛：%s\n", e.what());
  }
  std::printf("队列里 %zu 件\n", nq.size());
  return 0;
}
