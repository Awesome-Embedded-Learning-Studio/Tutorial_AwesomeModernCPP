---
title: "自行完成并发设计"
description: "8.1 立的考卷怎么落成一份能跑的设计：队列怎么建、一把锁管到哪、条件变量与 jthread 凭什么进场，原子与 future 缺席的理由也交代，每个决定带着回指前七单元的判据。"
chapter: 5
order: 2
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

# 自行完成并发设计

上一篇《[题目与行为要求](01-requirements)》把考卷立起来了：生产者提交任务，工作线程领走执行，收场交一行汇总，停止随时可能到来。8.1 还留了话：碰头的地方自己写还是拿现成的凑，哪些工具用、哪些不用，理由归本篇交代。现在轮到咱们把设计交出来。

我交的是示范，示范的意思得交代清楚：理由比答案值钱。8.1 的骨架形状本篇大体接住，只有一处接不住，我补了一个入口，理由在正文里摆着。8.4《提交验证与设计说明》（还没成篇）要您交的设计说明，考的就是这里的为什么。

设计的全部验证在临时目录 /tmp/concurrency-u8/ 里编译运行，机器是 WSL2，16 逻辑核，编译器 GCC 16.2.1 20260810，选项 `g++ -std=c++20 -O2 -pthread`，日期 2026-11-17。普通构建正常收场与中途停止各跑了两轮，TSan 复跑同样两轮，正文贴出的是代表性一轮，都没有报告。工具没说话不等于证明无错，能力边界 6.1《[怎样寻找并发错误](../chapter06-validating-designs/01-finding-concurrency-bugs)》交代过。语言与库的断言我按 cppreference 核对过，访问日期 2026-11-17。

## 从考卷到设计的输入

考卷给设计定下的边界，8.1 写得明白：任务内容是 `compute(i)`，算一次平方，快到几乎没有。13 的正倍数注定抛异常，失败是正常结果，要能对上号。提交与执行并发，中间必须有安全碰头的地方。停止提出后，已开跑的跑完，没开跑的计 skipped。收场之后守恒，提交数等于成功加失败加没做数。它们一条条都是判据的落点，咱们挨个看设计怎么接。

有一个缺口，是我把 `batch_processor.hpp` 翻了两遍才认出来的。8.1 的骨架只有 submit、request_stop、wait 三个动作，正常收场（无停止、skipped 为 0）走哪条路？工作线程挂在等待上，它得知道“不会再有新活了”才肯退。停止入口干不了该活：request_stop 的语义是没开跑的计 skipped，提交完拿它收场会把队尾的活冤枉成 skipped，违反考卷的对照要求。想到这里，我决定给形状加一个入口 `submissions_done()`：生产者交完喊一声，工作线程才知道收工。8.1 说过骨架是形状参考不是规定。

## 任务划分：常驻线程，一任务一线程出局

最省事的划分是一任务一线程：提交一万项，开一万条线程。它出局的理由 1.2 讲过（见《[线程在机器上是什么](../chapter01-start-concurrency/02-what-is-a-thread)》）：1:1 模型下每条线程都向内核要登记、要栈空间，7.1《[为什么需要暂停与恢复](../chapter07-coroutines/01-why-suspend-resume)》量过一千条线程就预订约 8 GB 地址空间。一万条线程换一万次乘法，不用算就看得见亏。所以咱们常驻 w 条工作线程（w 是考卷的输入参数），活进队列，线程循环领活。这就是线程池的雏形，生产级线程池不进必修，本篇交的是“每程序一组常驻线程”的最小形态，调度、扩缩、维护一概没有。

生产者单独一条线程也是考卷给咱们定的：提交与执行并发。生产者只管逐项 submit，快慢与交错交给运行时。工作线程的数量 w 在演示里按 `hardware_concurrency()` 的提示值起步（1.1《[什么时候值得引入并发](../chapter01-start-concurrency/01-when-concurrency)》讲过它只是提示，算不出来返回 0），回退到 4。完整版照考卷从命令行拿。

`compute` 是一次乘法，逐项领活每项要进一次锁，协调比活本身贵得多。我说的是推断，8.4 的读数来验。6.3 的判据是量在前改在后（见《[怎样调整设计](../chapter06-validating-designs/03-adjusting-design)》），所以我把立场摆在最直的形状上：把正确性立住，批量提交、批量领取之类的优化留给 8.4 的读数说话。一万次乘法是微秒级的活，协调全是纯开销，练习的价值在组织与验证，收益对比的读法归 6.2《[怎样衡量并发带来的收益](../chapter06-validating-designs/02-measuring-benefits)》。

## 数据所有权：一把锁一个保护域

任务的数据是枚编号，您在 8.1 见过：Job 按值进队列，没有共享数组，所有权交接就是队列的一推一弹。骨架里 Job 的 output 字段本设计没有用到，形状照 8.1 原样保留，结果直接进统计，将来要逐项留结果的实现再启用它。

麻烦的在于状态。队列、收工标志、停止标志、四个计数器、失败清单，它们全是共享可变状态，全放哪？我把多把锁的方案摆出来又否掉了：按对象一人一把锁的话，守恒等式每次更新要按顺序拿好几把锁，死锁的窗口和计数的窗口一起进来。2.3 的判据（见《[一次操作的边界在哪里](../chapter02-sharing-data/03-check-then-act)》）说这是多对象不变量，得让它们待在同一个保护域里。所以我拍板一把锁：`std::mutex` 一把，上述状态全部收进它的保护域。保护域的意思平白：谁拿到这把锁，谁才能动这些状态。一把锁之下，等式的四个数永远在同一次更新里变。

锁的范围我也按判据裁。领活、写回、置状态都拿锁，`compute` 在锁外跑（2.2《[用互斥保护共享状态](../chapter02-sharing-data/02-mutex-and-locks)》立过锁与数据的对应，6.3 量过活在锁外的收益）。锁内只做挪队列、加计数、记清单，都是快事，微秒级收尾。

收场之后的读数为什么免锁？wait 之后的 `total()`、`failures()` 等访问器，join 已经把工作线程的写入同步给调用线程（5.2 的 happens-before，见《[一个线程怎样观察另一个线程的修改](../chapter05-atomic-memory-model/02-happens-before)》），写者全退场，读是独占的。我要交代的是 const 不等于线程安全：没 wait 就读是调用违约，安全来自收场时序，不来自限定符。

## 协作方式：条件变量与 jthread 进场，原子与 future 缺席

碰头的地方，我拿互斥加条件变量建：队列本体的并发访问靠互斥，工作线程没有活时挂在条件变量上，不烧 CPU 空转。判据在 3.1（见《[等待一个条件成立](../chapter03-wait-and-coordinate/01-wait-for-a-condition)》）：阻塞着的等待方在场，轮询的代价 3.1 量过，CPU 白烧、察觉还慢。工作线程的谓词是三态：队列有活、生产者收工、停止已提出，醒了重查，虚假唤醒落进来只是多查一回。

丢通知的防法，咱们也照 3.1 的判据摆：状态的改动全部在锁内完成，通知放在改动之后。submit 里 push 在锁内、notify_one 在锁外，改动已对锁内查谓词的人可见，通知只是叫醒。request_stop 里置位在锁内、唤醒紧随其后。顺序对上，丢失唤醒就没有落脚的地方。

停止协议走 1.5 备好的工具（见《[让工作有序结束](../chapter01-start-concurrency/05-jthread-and-stopping)》）：处理程序里藏一个 `std::stop_source`，工作线程的等待用的是 `condition_variable_any` 带 `std::stop_token` 的重载（C++20 起，cppreference 写明 stop 请求会唤醒等待中的线程），停止提出时挂在 wait 上的人会被叫起来重查谓词。8.1 说过 stop_token 就是照协作式收场的语义造的工具，用不用归您、语义必须达到，本篇用它的理由是唤醒与语义都由标准替咱们对齐。手搓 `atomic<bool>` 标志的路也通，差在哪，可以不一样一节交代。

有一条判据在本设计里几乎没花力气，就同时成立了两件事，我把它单独摆出来。第一件，停止语义：考卷要求“还没被开跑的任务不再执行”，这意味着停止的置位与领活的动作必须互斥，不然置位与领走擦肩而过，冤枉一个任务开跑。置位与领活同用队列锁，互斥就是判定本身，谁也不会擦肩。第二件，原子与内存序的缺席：我拿 5.3 的数桥三问（见《[选择内存序需要什么依据](../chapter05-atomic-memory-model/03-choosing-memory-orders)》）数了一遍，桥的两端是谁、桥上过什么货、操作站在链条哪一段，数出来的答案是读写全在锁内互相排他，锁的进出已经把同步接好，原子档位成了重复的保护。8.1 数过的六样工具里原子也在列，判据判它不进场。

future 也缺席。骨架的 submit 不逐项回传结果，汇总收拢在处理程序自己的计数里。8.1 说的“失败跟结果走同一条路”，落在这里：失败与成功都进同一个保护域，送到汇总。逐项配一份 promise 加 future 的话，一万项任务就是一万份共享状态，重量花在没有要求的地方。future 什么时候值得进场？我给的判据是：结果要单独取回（4.1 的定位），或者任务要异步组装成链（4.3 的结果传递链的组织模式）。机制的出处请移步《[用 `std::async` 执行计算](../chapter04-organizing-tasks/01-launch-with-async)》。考卷没提要求，我就不添。

协程我留在了选做里，理由回指 7.1（见《[为什么需要暂停与恢复](../chapter07-coroutines/01-why-suspend-resume)》）：协程改变程序的组织，不添并行度，`compute` 里没有可以让出的等待点。8.1 的选做扩展（拿 7.4《[把协程组织成可完成的任务](../chapter07-coroutines/04-task-type)》的 Task 把提交与等待组织成协程）是它的位置，做完正好跟线程版对照谁在等待。

## 骨架与实测

下面是设计骨架 exp_a_design.cpp 的全文，正文里每个决定都标了判据出处，您可以停下来通读一遍。它还是演示版：n 与 w 写死在 main 里，没有走 8.1 定的命令行输入，完整版按行为要求补上，归 8.3。

```cpp
// exp_a_design.cpp —— 8.2 设计示范：单锁队列 + jthread 协作停止 + 锁内守恒
// 题目（8.1）：生产者提交编号任务，工作线程算 i*i，13 的正倍数失败，支持按条件停止
// 形状接 8.1 的 batch_processor.hpp，补一个 submissions_done 入口，理由在正文
#include <cassert>
#include <condition_variable>
#include <cstdio>
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

int main(int argc, char** argv) {
  const int n = 10000;                  // 8.1 的示例规模
  bool demo_stop = argc > 1 && argv[1][0] == 's';   // 传 s：提交过半时提出停止
  unsigned hw = std::thread::hardware_concurrency();
  unsigned w = hw > 0 ? hw : 4;         // 提示值起步，回退 4（1.1 的判据）

  {
    BatchProcessor bp(w);
    std::thread producer([&] {
      for (int i = 0; i < n; ++i) {
        bp.submit(Job{i, 0});
        if (demo_stop && i == n / 2) bp.request_stop();   // 停止落在提交过半的时机
      }
      bp.submissions_done();
    });
    producer.join();
    bp.wait();
    std::printf("w=%u total=%lld success=%zu failed=%zu skipped=%zu\n",
                w, bp.total(), bp.success_count(), bp.failed_count(), bp.skipped_count());
    auto fs = bp.failures();            // 失败能对上号：顺序不定，逐条可查
    if (!fs.empty())
      std::printf("first failure: input=%d reason=%s\n", fs.front().input, fs.front().reason.c_str());
  }

  // 设计自检：串行对照（对照手段归 8.4 的正确性检查，设计阶段就该在场）
  long long ref_total = 0;
  std::size_t ref_ok = 0, ref_fail = 0;
  for (int i = 0; i < n; ++i) {
    try { ref_total += compute(i); ++ref_ok; }
    catch (const std::exception&) { ++ref_fail; }
  }
  std::printf("serial   total=%lld success=%zu failed=%zu\n", ref_total, ref_ok, ref_fail);
  return 0;
}
```

咱们把普通构建跑了正常与停止各两轮，下面贴出的是代表性一轮：

```bash
g++ -std=c++20 -O2 -pthread -Wall -Wextra exp_a_design.cpp -o exp_a_design
./exp_a_design
./exp_a_design s
```

```text
w=16 total=307615387695 success=9231 failed=769 skipped=0
first failure: input=13 reason=编号 13 的倍数注定失败
serial   total=307615387695 success=9231 failed=769
w=16 total=38476928460 success=4617 failed=384 skipped=4999
first failure: input=13 reason=编号 13 的倍数注定失败
serial   total=307615387695 success=9231 failed=769
```

TSan 咱们也各跑两轮，都没有报告：

```bash
g++ -std=c++20 -O1 -g -pthread -fsanitize=thread exp_a_design.cpp -o exp_a_tsan
./exp_a_tsan
./exp_a_tsan s
```

正常收场的读数，跟 8.1 给的对照值一字不差（total=307615387695、success=9231、failed=769、skipped=0），串行对照也在场对数。停止一轮，停止落在提交过半的时机，成功、失败、skipped 三个数都非零，wait 里的守恒断言走完全程没有吭声。skipped 的读数取决于停止落地时队列还剩多少活，换个时机或换台机器就可能不同，我给的是单次读数。

失败的处理位置，我也单独交代一句。考卷要求一条失败别的照常，所以 try 落在单条任务的边界上：compute 抛的异常被工作线程按条接住，编号与消息存进 Failure，值拷贝，消息在异常对象销毁后还活着（8.1 点过的悬垂险情）。

## 可以不一样：别的路各在什么时候值得

示范定的每个决定都有替代方案，各自的入场条件我列在这里：

- 手搓停止标志替 stop_token：`atomic<bool>` 加一把通知用的锁也能达到语义，置位与领活照样要互斥，唤醒要提防丢失。C++20 不可用时的正路，1.5 交代过原子标志轮询的不足。
- 有界队列替无界 deque：生产消费速率差大、要背压的时候（3.2 的主场，见《[写一个有容量限制的任务队列](../chapter03-wait-and-coordinate/02-bounded-queue)》）。本题 n 已知、活全在内存，无界够用且少一个等待分支。
- 各线程私有统计、wait 时合并替共享计数：任务重、锁竞争可测的时候（2.4 的独立持有，跑完再合并，见《[减少共享与避免死锁](../chapter02-sharing-data/04-sharing-less-and-deadlock)》，6.3 量过锁外合并的收益）。守恒的核对挪到合并点，等式的四个数在收场凑齐。
- `std::async` 一任务一份 future：任务少而重、结果要单独取回的时候（4.1）。本题一万份共享状态不值，理由前面摆过。
- 批量提交、批量领取：粒度优化的正路，等 8.4 的读数指认协调是瓶颈再动（6.3 的量在前改在后）。

## 轮到您动手的时候

1. 把 done_ 挪出锁外改成 `atomic<bool>`，写一个测试查停止语义：停止提出后领走的任务编号记录下来，跑多轮找出开跑时刻晚于置位的漏网项。找到窗口，判据就归您了。
2. 手搓 atomic 加通知锁的停止版实现一遍，跟 stop_token 版对读数，体会标准集成替咱们省下的细节。
3. 把统计挪成各线程私有、wait 时合并，正常轮的读数必须不变（守恒等式在合并点核对）。8.4 拿它跟共享统计版对计时。
4. closed_ 之后再提交的违约处理换成返回 bool 或抛异常，对照 8.1 的退出码要求想一想哪种更合身。

## 下一步

设计交了，考卷还剩一半。失败清单怎么完整报告、停止怎么在多个时机注入、部分完成的结果怎么交代，归 8.3《处理完整的运行过程》（还没成篇）。骨架里我留了四处口子，对应的位置数得出来。`failures()` 是全量访问器，8.3 报告失败清单时用。producer 循环里写死 `i == n / 2` 的位置是停止注入点，8.3 换成多时机。Job 的 output 字段留而未启用。main 里的 n 与 w 没走命令行，8.3 按行为要求补上。

## 参考资源

推进思路参照 Williams《C++ Concurrency in Action》讲线程池与条件变量的章节，借的是结构与讲法，例子与文字都是我自己写的。等待、停止、粒度、锁外合并的判据分别立在 3.1《[等待一个条件成立](../chapter03-wait-and-coordinate/01-wait-for-a-condition)》、1.5《[让工作有序结束](../chapter01-start-concurrency/05-jthread-and-stopping)》、6.2《[怎样衡量并发带来的收益](../chapter06-validating-designs/02-measuring-benefits)》、6.3《[怎样调整设计](../chapter06-validating-designs/03-adjusting-design)》，本篇复用判据，读数全是本篇新测的。语言与库的断言按 cppreference 核对，访问日期 2026-11-17：

- [condition_variable_any::wait（cppreference）][cppref-cva-wait]，带 stop_token 的等待重载自 C++20 起、stop 请求唤醒等待者的出处。
- [std::condition_variable_any（cppreference）][cppref-cva]，通用版条件变量与 notify 的语义出处。
- [std::stop_source 与协作式停止（cppreference）][cppref-stop]，stop_token 与 stop 请求的语义出处，1.5 的工具。
- [std::async（cppreference）][cppref-async]，缺席理由里 future 与共享状态重量的参照。
- [std::thread::hardware_concurrency（cppreference）][cppref-hw]，提示语义与算不出来返回 0 的出处。

[cppref-cva-wait]: https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait
[cppref-cva]: https://en.cppreference.com/w/cpp/thread/condition_variable_any
[cppref-stop]: https://en.cppreference.com/w/cpp/thread/stop_source
[cppref-async]: https://en.cppreference.com/w/cpp/thread/async
[cppref-hw]: https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency
