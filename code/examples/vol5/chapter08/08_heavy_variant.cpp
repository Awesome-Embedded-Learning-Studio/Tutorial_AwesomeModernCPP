// heavy_variant.cpp —— 8.4 重活变体：compute 换带权重的循环版，设计一行不动
// 每项任务做 iters 次乘加，失败路径照旧；循环结果交给 do_not_optimize 看一眼
// （承 6.2 计时架子的写法，防编译器把加重循环整个删掉）。
// 默认 iters=200000、n=10000：串行约一秒，并发约零点二到零点四秒，在线跑得完。
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// heavy 变体相对 batch_design.hpp 唯一的差异，iters 由命令行传入
inline long long g_heavy_iters = 1;

template <class T>
inline void do_not_optimize(T& value) {
  asm volatile("" : "+m,r"(value) : : "memory");
}

inline long long compute(int i) {
  if (i % 13 == 0 && i > 0) throw std::runtime_error("编号 13 的倍数注定失败");
  long long s = 0;
  for (long long k = 1; k <= g_heavy_iters; ++k) s += (i * k) % 7;
  do_not_optimize(s);
  return s;
}

struct Job {
  int input{};          // 交给任务的编号
  long long output{};   // 任务算完写回的结果
};

// 一条失败记录：哪一项、抛了什么消息（消息存值，8.1 的要求）
struct Failure {
  int input{};
  std::string reason{};
};

class BatchProcessor {
public:
  explicit BatchProcessor(unsigned worker_count) {
    for (unsigned w = 0; w < worker_count; ++w)
      workers_.emplace_back([this] { worker_loop(); });
  }

  ~BatchProcessor() {          // 没人收场就析构：放弃剩余任务，兜底收场（RAII，1.4）
    request_stop();
    wait();
  }

  BatchProcessor(const BatchProcessor&) = delete;
  BatchProcessor& operator=(const BatchProcessor&) = delete;

  void submit(Job job) {
    bool notify = false;
    {
      std::lock_guard<std::mutex> lk(mu_);
      assert(!closed_ && "submissions_done 之后再提交是调用方违约");
      ++submitted_;
      if (done_) {             // 停止后提交的没开跑，直接计 skipped（8.1 的停止语义）
        ++skipped_;
      } else {
        queue_.push_back(std::move(job));
        notify = true;
      }
    }
    if (notify) cv_any_.notify_one();   // 状态改动已在锁内完成，通知放锁外不丢（3.1）
  }

  void submissions_done() noexcept {   // 8.1 骨架缺的口子：不会再有新活了
    {
      std::lock_guard<std::mutex> lk(mu_);
      closed_ = true;
    }
    cv_any_.notify_all();
  }

  void request_stop() noexcept {
    {
      std::lock_guard<std::mutex> lk(mu_);
      done_ = true;   // 判定点：置位与领活同锁，置位后不可能再领走任务（停止语义）
    }
    stop_src_.request_stop();   // 唤醒挂在 wait 上的工作线程（stop 集成，C++20）
  }

  void wait() {
    if (joined_) return;
    joined_ = true;
    for (auto& t : workers_) t.join();   // join 之后没有写者，收场读数免锁（5.2）
    std::lock_guard<std::mutex> lk(mu_);
    skipped_ += queue_.size();           // 停止收场：队列残留没开跑，清点进 skipped
    queue_.clear();
    assert(submitted_ == static_cast<long long>(success_ + failed_ + skipped_)
           && "守恒：提交数 = 成功 + 失败 + 没做");
  }

  long long total() const { return total_; }
  std::size_t success_count() const { return success_; }
  std::size_t failed_count() const { return failed_; }
  std::size_t skipped_count() const { return skipped_; }
  std::vector<Failure> failures() const { return failures_; }

private:
  void worker_loop() {
    std::stop_token tok = stop_src_.get_token();
    for (;;) {
      Job job;
      {
        std::unique_lock<std::mutex> lk(mu_);
        // 三态谓词：有活 / 收工 / 停止。stop 集成让 stop 请求也能唤醒（C++20 重载）
        cv_any_.wait(lk, tok, [&] { return !queue_.empty() || closed_ || done_; });
        if (done_ || queue_.empty()) return;   // 停止收场，或 closed 且取空
        job = std::move(queue_.front());
        queue_.pop_front();
      }
      try {                                    // 失败粒度是条：一条失败不拖累别人
        long long v = compute(job.input);      // compute 在锁外（6.3：活在锁外）
        std::lock_guard<std::mutex> lk(mu_);
        total_ += v;
        ++success_;
      } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lk(mu_);
        failures_.push_back(Failure{job.input, e.what()});
        ++failed_;
      }
    }
  }

  std::deque<Job> queue_;
  mutable std::mutex mu_;               // 唯一的锁：队列、状态、计数、清单全在它的保护域
  std::condition_variable_any cv_any_;  // 通用版：wait 能带 stop_token（C++20）
  std::stop_source stop_src_;
  std::vector<std::jthread> workers_;
  bool closed_ = false;                 // 锁内读写：不会再有新活
  bool done_ = false;                   // 锁内读写：停止已提出（语义判定点）
  bool joined_ = false;
  long long submitted_ = 0;
  long long total_ = 0;
  std::size_t success_ = 0, failed_ = 0, skipped_ = 0;
  std::vector<Failure> failures_;
};

struct Summary8 {
  long long total = 0;
  std::size_t success = 0, failed = 0, skipped = 0;
};

int main(int argc, char** argv) {
  long long iters = argc > 1 ? std::atoll(argv[1]) : 200000;
  const int n = 10000;
  unsigned hw = std::thread::hardware_concurrency();
  unsigned w = hw > 0 ? hw : 4;
  g_heavy_iters = iters;

  using clock_type = std::chrono::steady_clock;
  std::printf("[heavy variant, iters=%lld per task]\n", iters);

  {  // 串行一轮
    auto t0 = clock_type::now();
    Summary8 s;
    for (int i = 0; i < n; ++i) {
      try { s.total += compute(i); ++s.success; }
      catch (const std::exception&) { ++s.failed; }
    }
    auto ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
                  clock_type::now() - t0).count();
    std::printf("serial   n=%d round=0 %.3f ms\n", n, ms);
  }
  {  // 并发一轮（端到端，含线程建收）
    auto t0 = clock_type::now();
    BatchProcessor bp(w);
    std::thread producer([&] {
      for (int i = 0; i < n; ++i) bp.submit(Job{i, 0});
      bp.submissions_done();
    });
    producer.join();
    bp.wait();
    auto ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
                  clock_type::now() - t0).count();
    std::printf("parallel n=%d w=%u round=0 %.3f ms\n", n, w, ms);
  }
  return 0;
}
