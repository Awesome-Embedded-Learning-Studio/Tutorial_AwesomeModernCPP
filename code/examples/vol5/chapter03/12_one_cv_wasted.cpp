// one_cv_wasted.cpp —— 一把条件变量的队列：醒了条件还不成立，白跑一趟能数出来
// 负载与 3.2 量白跑的实验同款：容量 4，2 个生产者各放 4000 件，2 个消费者各取 4000 件
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// 一把 cv 的队列：生产者与消费者睡在同一把 cv 上，notify_one 不看条件挑人，
// 醒来重看条件还不成立就白跑一趟，两侧各自计数
class OneCvQueue {
public:
  explicit OneCvQueue(std::size_t capacity) : capacity_(capacity) {}

  void push(int v) {
    std::unique_lock<std::mutex> lk(m);
    while (q.size() >= capacity_) {
      cv.wait(lk);
      if (q.size() >= capacity_) ++wasted_push;   // 醒了，位置还没空出来，白跑一趟
    }
    q.push(v);
    cv.notify_one();
  }

  int pop() {
    std::unique_lock<std::mutex> lk(m);
    while (q.empty()) {
      cv.wait(lk);
      if (q.empty()) ++wasted_pop;   // 醒了，条件还不成立，白跑一趟
    }
    int v = q.front();
    q.pop();
    cv.notify_one();
    return v;
  }

  // 次数都在锁内累加，join 完所有线程再读
  long long wasted_pushes() const { return wasted_push; }
  long long wasted_pops() const { return wasted_pop; }

private:
  std::queue<int> q;
  std::size_t capacity_;
  std::mutex m;
  std::condition_variable cv;
  long long wasted_push = 0;   // 生产侧白跑
  long long wasted_pop = 0;    // 消费侧白跑
};

int main() {
  OneCvQueue q(4);
  constexpr int kProducers = 2;
  constexpr int kConsumers = 2;
  constexpr int kPerSide = 4000;

  std::vector<std::thread> threads;
  for (int i = 0; i < kConsumers; ++i) {
    threads.emplace_back([&q] {
      for (int k = 0; k < kPerSide; ++k) q.pop();
    });
  }
  for (int i = 0; i < kProducers; ++i) {
    threads.emplace_back([&q, i] {
      for (int k = 0; k < kPerSide; ++k) q.push(i * kPerSide + k);
    });
  }
  for (auto& t : threads) t.join();

  std::printf("一把 cv : 生产侧白跑 %6lld 次  消费侧白跑 %6lld 次\n",
              q.wasted_pushes(), q.wasted_pops());
  return 0;
}
