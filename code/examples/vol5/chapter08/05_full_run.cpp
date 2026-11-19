// full_run.cpp —— 8.3 完整运行：接住 8.2 留的四处口子，把 8.1 的行为要求跑通
// 题目（8.1）：生产者提交编号任务，工作线程算 i*i，13 的正倍数失败，支持按条件停止
// 设计（8.2）：单锁队列 + jthread 协作停止 + 锁内守恒，本篇不改设计，只补口子
// 口子一：failures() 全量清单在 main 里全量输出
// 口子二：producer 循环写死的 i == n/2 换成多时机注入（early/mid/tail/backlog）
// 口子三：Job.output 启用，逐项结果留档，收场后走第二条路核对汇总
// 口子四：n 与 w 从命令行收，不再写死
#include <cassert>
#include <climits>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

// 一项任务的输入与结果
struct Job {
  int input{};          // 交给任务的编号
  long long output{};   // 任务算完写回的结果（口子三：8.3 启用）
};

// 一条失败记录：哪一项、抛了什么消息（消息存值，8.1 的要求）
struct Failure {
  int input{};
  std::string reason{};
};

// 停止注入的时机（口子二）：none 不停；early/mid/tail 在 producer 循环的对应位置提出；
// backlog 在全部提交完成、收场等待未启动时提出，队列里压着活
enum class StopWhen { None, Early, Mid, Tail, Backlog };

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

  void submissions_done() noexcept {   // 不会再有新活了（8.2 补的入口）
    {
      std::lock_guard<std::mutex> lk(mu_);
      closed_ = true;
    }
    cv_any_.notify_all();
  }

  void request_stop() noexcept {
    {
      std::lock_guard<std::mutex> lk(mu_);
      done_ = true;   // 置位与领活同锁，置位后不可能再领走任务（停止语义）
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
  std::vector<Failure> failures() const { return failures_; }   // 全量清单（口子一）
  std::vector<Job> results() const { return results_; }         // 逐项结果（口子三）

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
        job.output = compute(job.input);       // compute 在锁外（6.3：活在锁外），结果写回 output
        std::lock_guard<std::mutex> lk(mu_);
        total_ += job.output;
        ++success_;
        results_.push_back(job);               // 逐项留档，收场后供第二条路核对
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
  std::vector<Job> results_;            // 逐项结果，完成序
};

// 口子二：时机串换成枚举，认不出返回 false
static bool parse_mode(const char* s, StopWhen& out) {
  if (s == nullptr || std::strcmp(s, "none") == 0) { out = StopWhen::None; return true; }
  if (std::strcmp(s, "early") == 0) { out = StopWhen::Early; return true; }
  if (std::strcmp(s, "mid") == 0)   { out = StopWhen::Mid;   return true; }
  if (std::strcmp(s, "tail") == 0)  { out = StopWhen::Tail;  return true; }
  if (std::strcmp(s, "backlog") == 0) { out = StopWhen::Backlog; return true; }
  return false;
}

int main(int argc, char** argv) {
  if (argc < 3) {   // 口子四：n 与 w 从命令行收，缺参报错退出（8.1 允许自定退出码）
    std::fprintf(stderr, "usage: %s <n> <w> [none|early|mid|tail|backlog]\n", argv[0]);
    return 1;
  }
  char* end = nullptr;
  long long n_ll = std::strtoll(argv[1], &end, 10);
  if (end == argv[1] || *end != '\0' || n_ll <= 0 || n_ll > INT_MAX) {
    std::fprintf(stderr, "bad n: %s（要 1 到 %d 的整数）\n", argv[1], INT_MAX);
    return 1;
  }
  const int n = static_cast<int>(n_ll);
  end = nullptr;
  long long w_ll = std::strtoll(argv[2], &end, 10);
  if (end == argv[2] || *end != '\0' || w_ll <= 0) {
    std::fprintf(stderr, "bad w: %s（要正整数）\n", argv[2]);
    return 1;
  }
  StopWhen mode = StopWhen::None;
  if (argc > 3 && !parse_mode(argv[3], mode)) {
    std::fprintf(stderr, "unknown mode: %s\n", argv[3]);
    return 1;
  }

  {
    BatchProcessor bp(static_cast<unsigned>(w_ll));
    std::thread producer([&] {
      for (int i = 0; i < n; ++i) {
        bp.submit(Job{i, 0});
        // 注入点（口子二）：位置由模式定，early/mid/tail 都在 producer 循环里
        bool hit = (mode == StopWhen::Early && i == 16)
                || (mode == StopWhen::Mid && i == n / 2)
                || (mode == StopWhen::Tail && i == n - 8);
        if (hit) bp.request_stop();
      }
      bp.submissions_done();
    });
    producer.join();
    if (mode == StopWhen::Backlog) bp.request_stop();   // 队列压着活的时机
    bp.wait();

    // 8.1 定的格式：一行汇总，字段与次序照考卷来
    std::printf("total=%lld success=%zu failed=%zu skipped=%zu\n",
                bp.total(), bp.success_count(), bp.failed_count(), bp.skipped_count());

    // 口子一启用：失败清单全量输出，哪一项、为什么，逐条可查
    auto fs = bp.failures();
    for (std::size_t k = 0; k < fs.size(); ++k)
      std::printf("failure[%zu] input=%d reason=%s\n", k, fs[k].input, fs[k].reason.c_str());

    // 口子三启用：逐项结果走第二条路核对——数目、求和、配对、编号不重
    auto items = bp.results();
    bool items_ok = items.size() == bp.success_count();
    long long sum = 0;
    std::vector<char> seen(static_cast<std::size_t>(n), 0);
    for (const Job& j : items) {
      sum += j.output;
      if (j.output != static_cast<long long>(j.input) * j.input) items_ok = false;
      if (j.input < 0 || j.input >= n || seen[static_cast<std::size_t>(j.input)]) items_ok = false;
      else seen[static_cast<std::size_t>(j.input)] = 1;
    }
    if (!items_ok || sum != bp.total()) {
      std::printf("item check: FAILED\n");
      return 2;
    }
    std::printf("item check ok: %zu results\n", items.size());

    // 串行对照：无 skipped 的轮次必须全对上（对照手段归 8.4 的正确性检查，自检在这里）
    long long ref_total = 0;
    std::size_t ref_ok = 0, ref_fail = 0;
    for (int i = 0; i < n; ++i) {
      try { ref_total += compute(i); ++ref_ok; }
      catch (const std::exception&) { ++ref_fail; }
    }
    std::printf("serial   total=%lld success=%zu failed=%zu\n", ref_total, ref_ok, ref_fail);
    if (bp.skipped_count() == 0
        && (bp.total() != ref_total || bp.success_count() != ref_ok || bp.failed_count() != ref_fail)) {
      std::printf("serial check: MISMATCH\n");
      return 2;
    }
  }
  return 0;
}
