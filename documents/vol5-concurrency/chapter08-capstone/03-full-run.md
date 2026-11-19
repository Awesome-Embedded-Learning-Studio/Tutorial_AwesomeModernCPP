---
title: "处理完整的运行过程"
description: "8.2 的设计示范跑成完整程序：四处口子逐个接住，汇总、失败清单与多时机停止全部实测，读数逐条对上 8.1 的行为要求。"
chapter: 5
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 处理完整的运行过程

上一篇《[自行完成并发设计](02-design)》把设计交了，结尾留了话：骨架里四处口子，等 8.3 来接。现在咱们就来接。口子接完，8.2 的设计示范长成完整可运行的程序：n 与 w 从命令行进来，跑完交一行汇总，失败清单全量输出，停止在多个时机注入实测。8.1《[题目与行为要求](01-requirements)》立下的行为要求，本篇逐条拿读数去对。

验证交代摆在最前面。完整程序 full_run.cpp 在临时目录 /tmp/concurrency-u8/ 里编译运行，机器是 WSL2，16 逻辑核，编译器 GCC 16.2.1 20260810，选项 `g++ -std=c++20 -O2 -pthread -Wall -Wextra`，日期 2026-11-18。正常轮、四种停止时机轮、TSan 复测全部当天跑完，正文贴出的读数是单次实测，读数会抖的地方我单独交代。编译命令没带 `-DNDEBUG`（它一定义，assert 就被整个编译掉），`wait()` 里的守恒断言在场干活，后文专门有一段让您看它拦下错误的实况。语言与库的断言我按 cppreference 核对过，访问日期 2026-11-18，出处收在参考资源里。

## 口子留了四处，挨个接上

8.2 的结尾数过位置，哪里留的、本篇怎么接，咱们在这里一次对齐：

- `failures()` 在 8.2 的演示里只打印了头一条失败。咱们让它交全量清单：main 把整份失败记录逐条输出，编号与消息一行一条，8.1 要求的“失败要能对上号”落在那里。
- 注入点此前写死在 producer 循环里，位置是 `i == n / 2`。本篇把它换成时机参数：early 在提交刚起步时停，mid 在提交过半时停（旧位置），tail 在提交只剩尾巴时停，backlog 在全部提交完成、收场等待未启动时停。
- `Job` 的 `output` 字段 8.2 留而未启用。咱们让工作线程把结果写回 `output` 并逐项留档，收场后拿逐项求和跟累加器对一遍，汇总多出一条核对的路。
- 8.1 定的输入是命令行，main 里 n 与 w 却写死了。咱们照改：n 与 w 必填，时机可选，缺参与坏参报错退出。

四处接线都在 main 与工作线程的成功路径上，设计本身一行没动：队列还是一把锁，停止还是 stop_source 加条件变量，守恒断言照旧住在 wait 里。您可以把 full_run.cpp 跟 8.2 的 exp_a_design.cpp 并排读，差别全在口子上。

## 完整程序：full_run.cpp

下面是程序全文。口子的位置我在注释里标了号，跟上面的清单对应，您顺着注释读，省得在散文与代码之间来回翻。

```cpp
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
```

注入点的挪法值得单独交代。early、mid、tail 三个模式仍在 producer 循环里提出停止，位置分别是 i 等于 16、n 的一半、n 减 8，16 与 8 是随手挑的两头位置，换别的数一样。backlog 的停止不在这循环里，它等 producer 全部交完、join 回来之后由 main 提出。安排的依据是 8.1 列出的三个时机：early 管提交刚起步，mid 管提交过半，tail 管提交只剩尾巴，三处全落在 producer 循环里，backlog 管队列压活的情形。队列要攒得住活，消耗端就得慢，backlog 轮的 w 我收到 1，一条工作线程慢慢消化，停止落地时队列里压着几千项。

output 的启用带来一条额外的核对路。工作线程算完把结果写进 job.output，锁内累加、计数、留档一次做完。收场之后咱们拿逐项清单走第二条路：逐项求和要等于累加器，逐项的 output 要对得上 input 的平方，编号要不重不漏，任何一条不成立就报 `item check: FAILED`、退出码 2。8.2 的版本只有累加器一条路，悄悄漏加一项它自己发现不了，两条路都走，丢掉的数字才藏不住。

命令行与输出都照 8.1 的输入输出行为收。n 与 w 必填，时机可选。认不出的时机串、不是正整数的参数，程序往标准错误流报一句、退出码 1，8.1 给了自定退出码的自由。汇总行的格式也从 8.2 的演示样式收了回来：8.2 打印 `w=16 total=...`，前缀里带线程数，8.1 的格式一行只有四个字段，咱们把前缀去掉，跟对照值逐字对得上才有意义。

串行对照的自检留在 main 里，8.2 已经在场，咱们沿用：程序收场后自己串行算一遍，skipped 为 0 的轮次要求三个数全等，不等就报 `serial check: MISMATCH`、退出码 2。skipped 大于 0 的停止轮不做全等断言，总数本来只含做成的那部分，对不上是预期，不是错误。

## 正常轮：汇总行对上对照值

咱们编好程序，拿 n 等于 10000、w 等于 16 跑基准轮，8.1 的对照值正是按此规模给的：

```bash
g++ -std=c++20 -O2 -pthread -Wall -Wextra full_run.cpp -o full_run
./full_run 10000 16
```

```text
total=307615387695 success=9231 failed=769 skipped=0
failure[0] input=13 reason=编号 13 的倍数注定失败
failure[1] input=26 reason=编号 13 的倍数注定失败
failure[2] input=39 reason=编号 13 的倍数注定失败
failure[3] input=52 reason=编号 13 的倍数注定失败
  （中段从 failure[4] input=65 到 failure[767] input=9984，逐行格式相同，我这里省略）
failure[768] input=9997 reason=编号 13 的倍数注定失败
item check ok: 9231 results
serial   total=307615387695 success=9231 failed=769
```

咱们把汇总行跟 8.1 的对照值并排一放，total、success、failed、skipped 四个数，逐项一字不差。串行对照行是程序自己算的，两边全等，退出码 0。失败清单 769 条全量输出，中段省略是我做的节选，不是程序。同一条命令咱们重复跑了三遍，读数逐轮一致，skipped 为 0 时全量任务都必须完成，交错再乱，结果也定得住。

### 自检要能抓到错才算数

我做了两个故意弄坏的版本，各跑一轮，让您看看自检拦下错误的样子。

一个版本，咱们把累加语句改成编号 4242 的结果不进 total_，其余照旧。逐项求和与累加器对不上，程序当场报 `item check: FAILED`，退出码 2。8.2 只有累加器一条路的版本里，类似的漏加谁都发现不了，逐项留档的价值就落在这里。

另一个版本，咱们让工作线程领到编号 42 就直接做下一项，哪个计数都不进。wait() 里的守恒断言当场拦下，进程中止（SIGABRT，退出码 134）。看到它当场喊停，我心里反而踏实，守恒真有人管：

```text
broken_b.cpp:89: void BatchProcessor::wait(): Assertion `submitted_ == static_cast<long long>(success_ + failed_ + skipped_) && "守恒：提交数 = 成功 + 失败 + 没做"' failed.
```

两个坏版本都是 2026-11-18 当天实测的。输出里的行号指向 broken_b.cpp 第八十九行，坏版在 compute 调用之前插了一行 continue，行号与正文程序对不上，您找代码时认准文件名。断言与退出码的分工咱们也看得清楚：单项数字对不上，走退出码 2 的温和路。任务整个下落不明，守恒断言直接中止程序。8.4《[提交验证与设计说明](04-verification)》的正式验收会把这些自检升级成逐行给读数的检查。

## 失败清单：全量、能对上号

上一轮 769 条的中段省了，要看清单长什么样，咱们拿小规模跑一轮：n 等于 40，13、26、39 三个注定失败的编号全在列：

```bash
./full_run 40 4
```

```text
total=18174 success=37 failed=3 skipped=0
failure[0] input=13 reason=编号 13 的倍数注定失败
failure[1] input=26 reason=编号 13 的倍数注定失败
failure[2] input=39 reason=编号 13 的倍数注定失败
item check ok: 37 results
serial   total=18174 success=37 failed=3
```

每条记录都有编号与消息，消息是异常对象的拷贝，异常销毁之后还活着，8.1 点过的悬垂险情在这里站不住。清单的次序是完成序：几条线程同时消化，异常冒出来的次序就是记录的次序。n 等于 10000 的轮次里清单恰好按编号升序排出来（13、26、39、52、65 开头，9958、9971、9984、9997 收尾），那是本机当天的巧合，不是承诺。您要按编号看，拿 failures() 返回的拷贝排序就好，别动处理器内部的原表。

## 停止轮：多时机注入

8.1 写明停止随时可能到来，并列出三个时机：提交进行到一半、队列里压着活、最后几项刚开跑。我把时机做成命令行参数，四个模式各跑一轮，正常轮做基准。backlog 模式的 w 我收到 1，原因前面交代过，队列得攒得住活，停止落地才有压着的活可清点。

```bash
./full_run 10000 16            # 不停（基准）
./full_run 10000 16 early      # 提交刚起步：producer 在 i == 16 处提出停止
./full_run 10000 16 mid        # 提交过半：i == n/2，8.2 的老位置
./full_run 10000 16 tail       # 提交只剩尾巴：i == n-8
./full_run 10000 1 backlog     # 全部交完、收场等待未启动，w=1 让队列攒活
```

| 模式 | 汇总行（实测） |
|---|---|
| 不停 | `total=307615387695 success=9231 failed=769 skipped=0` |
| early | `total=1327 success=16 failed=1 skipped=9983` |
| mid | `total=38451928460 success=4616 failed=384 skipped=5000` |
| tail | `total=307015887564 success=9225 failed=768 skipped=7` |
| backlog | `total=3701128302 success=2116 failed=176 skipped=7708` |

early 轮停止落在 i 等于 16，咱们数得清：编号 0 到 16 共 17 项在停止之前进了队列，16 项成功加编号 13 的 1 项失败，正好 17 项。编号 17 以后的 9983 项在 submit 里读到停止已置位，直接计进 skipped。

tail 轮的停止落在 i 等于 9992，咱们对得更快：编号 0 到 9992 共 9993 项全部消化完（9225 成功加 768 失败），末尾 7 项提交时计 skipped，wait 收场时队列是空的。

backlog 轮走得最慢，咱们看得最直观：10000 项全部提交完停止才落地，单条工作线程消化了 2292 项（2116 加 176），队列里剩下的 7708 项由 wait 清点进 skipped。每轮三数相加都等于 10000，wait 里的守恒断言走完全程没有吭声，五轮退出码全是 0。

抖动也要如实交代。咱们把同一条命令各重复跑了几轮，正常轮与 tail 轮的读数逐轮一致。early、mid、backlog 的成功数与 skipped 在我跑的这几轮里一轮一个样，early 的成功数在 13 到 16 之间，mid 在 4616 与 4617 之间，backlog 的幅度最大，从一千五百多挪到两千五百多，我头一回对出这个差值时愣了一下，转念一想，它的停止落地时机本来就最飘，抖得动才对。以上都是 2026-11-18 本机的观察。抖的是停止落地时交错里的位置，不抖的是三数守恒与退出码。8.2 说过停止轮的 skipped 取决于落地时队列剩多少活，这里就是实测版。

停止提出后，挂在条件变量上的工作线程能被叫起来，靠的是 condition_variable_any 带 stop_token 的等待重载，C++20 起可用，cppreference 写明该重载会登记停止请求的通知。置位与唤醒的分工还是 8.2 交代的形状：置位在锁内，唤醒紧随其后，判停的判定点也在锁内。咱们沿用判据不重开课，出处见参考资源。

## TSan 复测

工具复测照 6.1《[怎样寻找并发错误](../chapter06-validating-designs/01-finding-concurrency-bugs)》的手法来，咱们在 TSan 构建下把五个模式各跑一轮：

```bash
g++ -std=c++20 -O1 -g -pthread -fsanitize=thread full_run.cpp -o full_run_tsan
./full_run_tsan 10000 16
./full_run_tsan 10000 16 early
./full_run_tsan 10000 16 mid
./full_run_tsan 10000 16 tail
./full_run_tsan 10000 1 backlog
```

五轮退出码全是 0，一份报告都没有。TSan 把执行放慢，交错换了一批，读数跟着挪（backlog 轮在 TSan 下成功数到了 5252），守恒照旧成立。边界咱们把 6.1 的立场再摆一次：工具没报告不等于证明无错，它只是今天没话讲。

## 行为要求逐条对照

8.1 的行为要求逐条对下来，咱们把证据归进一张表，行行有出处：

| 8.1 的行为要求 | 本篇的实测证据 |
|---|---|
| 命令行收 n 与 w | 各轮命令行即证据，缺参、坏参、认不出的时机串报错退出 1 |
| 汇总行字段与次序 | 正常轮汇总行与 8.1 的对照值一字不差 |
| 汇总与串行对得上 | 正常轮 serial 行全等，不匹配即报 MISMATCH、退出码 2 |
| 重复运行结果一致 | 正常轮重复三遍读数一致，更多轮数的重复测试归 8.4 |
| 计数守恒 | wait 里的断言，全部轮次走完没有吭声 |
| 没开跑的不再执行、计 skipped | early、mid、tail、backlog 四轮的 skipped 读数与数字去处 |
| 已开跑的跑完、照常进汇总 | 停止轮的 item check 逐项核对照常通过 |
| 停止在哪个时机到都收得住 | 四种时机各一轮，守恒与退出码全数成立 |
| 异常被接住、失败隔离 | 正常轮 success=9231，769 条失败不拖累别的任务 |
| 失败能对上号 | failure[i] 的编号与消息逐条在列，n=40 轮三条全列 |
| 失败算正常结果 | failed 进守恒等式，含失败与停止的轮次退出码照旧 0 |

表里有一行咱们要再交代一句。重复一致的要求，轮数与工具归 8.4 安排，本篇只交了正常轮三遍与停止轮多轮的观察，正式的重复测试与插桩复跑读数看 8.4《[提交验证与设计说明](04-verification)》，两边合着看。

## 逐项留档的代价

口子三不是白接的，代价咱们也摆出来。逐项留档意味着 results_ 从任务完成到收场一直驻留内存：一个 Job 在本机 x86-64 上占 16 字节，n 等于 10000 时约 160 KB，本题无所谓，n 再涨两个量级，就该掂量了。失败清单同理，769 条记录驻留到收场。要是您只需要汇总，8.2 直接累加的形状更瘦，要逐项可查，驻留就是买核对路的价钱。两种形状跟着需求选，两版的开销差您把 n 与 w 改一改就能自量。

## 轮到您动手的时候

1. 把注入位置做成第四个命令行参数（任意 i），跑一组位置扫描，看 skipped 随位置怎么挪。找找读数从抖到稳的分界落在哪里，解释它跟队列存量有什么关系。
2. 把 backlog 模式的 w 换成 16 再跑几轮，观察 skipped 还剩多少。攒不攒得住活跟消耗端的速度直接相关，把您的读数跟 w=1 的对照一下。
3. 失败清单按编号排序输出：拿 failures() 返回的拷贝排序，别动处理器内部的原表，想想为什么完成序是并发事实、排序是展示的选择。
4. 把逐项留档撤掉，回到 8.2 直接累加的形状，正常轮的读数必须一字不差。改完各跑一轮计时，留档版与累加版差多少，您手里就有数了。

## 下一步

考卷的行为要求到本篇全部有了实测。咱们还剩最后一站：8.4《[提交验证与设计说明](04-verification)》把自检升级成正式验收，正确性检查与串行对比按 6.2《[怎样衡量并发带来的收益](../chapter06-validating-designs/02-measuring-benefits)》的测量条件立报告，重复测试与插桩复跑按 6.1 的手法进验证驱动，计时对比给出并发对串行的读数。8.1 验收表格里的每一行都要一份读数，本篇的自检、多时机注入与数字去处的走查正是它们的雏形，交卷时照行给证据就行。

## 参考资源

推进思路参照 Williams《C++ Concurrency in Action》讲线程池与协作停止的章节，借的是结构与讲法，例子与文字是我自己写的。设计判据出自 8.2《[自行完成并发设计](02-design)》，本篇沿用不重开。语言与库的断言按 cppreference 核对，访问日期 2026-11-18：

- [condition_variable_any::wait（cppreference）][cppref-cva-wait]，带 stop_token 的等待重载自 C++20 起、停止请求唤醒等待者的出处，停止轮唤醒的依据。
- [std::stop_source（cppreference）][cppref-stop]，request_stop 语义的出处。
- [std::jthread（cppreference）][cppref-jthread]，工作线程本体与析构兜底（RAII）的出处。

[cppref-cva-wait]: https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait
[cppref-stop]: https://en.cppreference.com/w/cpp/thread/stop_source
[cppref-jthread]: https://en.cppreference.com/w/cpp/thread/jthread
