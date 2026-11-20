// verify_all.cpp —— 8.4 验证驱动：一套程序装下正确性检查与计时对比
// 驱动包含的 batch_design.hpp 装的是 8.2 示范去掉 main 的全文：compute、Job、Failure
// 与 BatchProcessor 四件，原样搬入，只加了包含保护，其余一行没动。
// 下面贴的是 verify 模式（正确性部分）全文；bench 模式与参数解析是几行胶水，一并补齐。
// 默认参数：verify 20 轮、n=10000、w=hardware_concurrency（在线运行直接出读数）。
// 本地用法：verify_all [v <rounds> <n> <w> | b <n> <w>]，缺省 v 20 10000 <hw>。
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

// 考卷给定的任务内容（8.1 的考卷原文）
long long compute(int i) {
  if (i % 13 == 0 && i > 0) throw std::runtime_error("编号 13 的倍数注定失败");
  return static_cast<long long>(i) * i;
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

int g_failed_checks = 0;

void check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("CHECK FAILED: %s\n", what.c_str());
    ++g_failed_checks;
  }
}

struct Summary {
  long long total = 0;
  std::size_t success = 0, failed = 0, skipped = 0;
  bool same_as(const Summary& o) const {
    return total == o.total && success == o.success
        && failed == o.failed && skipped == o.skipped;
  }
};

static void print_summary(const char* tag, const Summary& s) {
  std::printf("%s total=%lld success=%zu failed=%zu skipped=%zu\n",
              tag, s.total, s.success, s.failed, s.skipped);
}

// 串行对照：8.1 expected.cpp 的算法原样，失败只计数，不进总数
Summary run_serial(int n) {
  Summary s;
  for (int i = 0; i < n; ++i) {
    try { s.total += compute(i); ++s.success; }
    catch (const std::exception&) { ++s.failed; }
  }
  return s;
}

// 守恒与失败清单，每一轮都查
void check_invariants(const BatchProcessor& bp, const Summary& s, int n, const char* tag) {
  check(s.success + s.failed + s.skipped == static_cast<std::size_t>(n),
        std::string(tag) + ": 守恒 提交数 = 成功 + 失败 + 没做");
  auto fs = bp.failures();
  check(fs.size() == s.failed, std::string(tag) + ": 失败清单条数 = failed 计数");
  bool all_thirteen = true, all_msg = true;
  for (const auto& f : fs) {
    if (f.input <= 0 || f.input >= n || f.input % 13 != 0) all_thirteen = false;
    if (f.reason != "编号 13 的倍数注定失败") all_msg = false;
  }
  check(all_thirteen, std::string(tag) + ": 失败清单每条都是 13 的正倍数");
  check(all_msg, std::string(tag) + ": 失败消息与考卷原文一字不差");
}

// 正常收场一轮：生产者单独线程逐项提交，交完喊收工，wait 等全部退场
Summary run_normal(int n, unsigned w) {
  BatchProcessor bp(w);
  std::thread producer([&] {
    for (int i = 0; i < n; ++i) bp.submit(Job{i, 0});
    bp.submissions_done();
  });
  producer.join();
  bp.wait();
  Summary s;
  s.total = bp.total();
  s.success = bp.success_count();
  s.failed = bp.failed_count();
  s.skipped = bp.skipped_count();
  check_invariants(bp, s, n, "normal");
  return s;
}

// 停止注入一轮：提交进行到第 stop_at 项时提出停止，stop_at == n 表示全部交完才停
Summary run_stop(int n, unsigned w, int stop_at, const char* tag) {
  BatchProcessor bp(w);
  std::thread producer([&] {
    for (int i = 0; i < n; ++i) {
      if (i == stop_at) bp.request_stop();
      bp.submit(Job{i, 0});
    }
    if (stop_at >= n) bp.request_stop();
    bp.submissions_done();
  });
  producer.join();
  bp.wait();
  Summary s;
  s.total = bp.total();
  s.success = bp.success_count();
  s.failed = bp.failed_count();
  s.skipped = bp.skipped_count();
  check_invariants(bp, s, n, tag);
  return s;
}

// verify 模式：串行对照、重复轮一致性、停止多时机，全查
int mode_verify(int rounds, int n, unsigned w) {
  const Summary ref = run_serial(n);
  print_summary("serial-ref", ref);
  int mismatch = 0;
  Summary last;
  for (int r = 0; r < rounds; ++r) {
    Summary s = run_normal(n, w);
    if (!s.same_as(ref)) ++mismatch;
    last = s;
  }
  print_summary("normal-last", last);
  std::printf("rounds=%d mismatch=%d\n", rounds, mismatch);
  check(mismatch == 0, "重复轮汇总轮轮一致");

  struct Timing { int at; const char* tag; };
  const Timing timings[] = {
      {0, "stop@0%"}, {n / 10, "stop@10%"}, {n / 2, "stop@50%"},
      {n * 9 / 10, "stop@90%"}, {n, "stop@done"}};
  for (const Timing& t : timings) {
    Summary s = run_stop(n, w, t.at, t.tag);
    print_summary(t.tag, s);
  }
  check(run_normal(n, w).same_as(ref), "时机组跑完后再跑正常轮仍与串行一致");
  return g_failed_checks;
}

// bench 模式：串行、并发、进场开销各量三轮（读数口径承 6.2，端到端含线程建收）
int mode_bench(int n, unsigned w) {
  using clock_type = std::chrono::steady_clock;
  for (int r = 0; r < 3; ++r) {
    auto t0 = clock_type::now();
    run_serial(n);
    auto ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
                  clock_type::now() - t0).count();
    std::printf("serial   n=%d round=%d %.3f ms\n", n, r, ms);
  }
  for (int r = 0; r < 3; ++r) {
    auto t0 = clock_type::now();
    run_normal(n, w);
    auto ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
                  clock_type::now() - t0).count();
    std::printf("parallel n=%d w=%u round=%d %.3f ms\n", n, w, r, ms);
  }
  for (int r = 0; r < 3; ++r) {
    auto t0 = clock_type::now();
    { BatchProcessor bp(w); }
    auto ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
                  clock_type::now() - t0).count();
    std::printf("pool-cost w=%u round=%d %.3f ms\n", w, r, ms);
  }
  return 0;
}

int main(int argc, char** argv) {
  unsigned hw = std::thread::hardware_concurrency();
  unsigned w = hw > 0 ? hw : 4;
  if (argc > 1 && argv[1][0] == 'b') {
    int n = argc > 2 ? std::atoi(argv[2]) : 10000;
    if (argc > 3) w = static_cast<unsigned>(std::atoi(argv[3]));
    return mode_bench(n, w);
  }
  int rounds = argc > 2 ? std::atoi(argv[2]) : 20;
  int n = argc > 3 ? std::atoi(argv[3]) : 10000;
  if (argc > 4) w = static_cast<unsigned>(std::atoi(argv[4]));
  return mode_verify(rounds, n, w);
}
