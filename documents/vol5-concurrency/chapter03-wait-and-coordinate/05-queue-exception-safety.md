---
title: "搬一半抛了算谁的：队列的异常安全"
description: "异常从构造、push 的拷贝、pop 的移动三处半路抛出来，队列里里外外各剩什么，逐个现场量一遍，收在两道编译期的闸与析构不抛的纪律上。"
chapter: 5
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 搬一半抛了算谁的：队列的异常安全

能关、能背压，还剩最刁的一类场面：异常从构造或者搬运的半路抛出来，队列里里外外算谁的。

## 异常安全

异常安全咱们查三处：构造，搬进搬出的每一步，析构。锁的释放是全场兜底，也一并说。

构造。咱们顺着声明顺序过成员。`std::queue` 首次分配可能抛 `bad_alloc`。`condition_variable` 的构造函数按标准可以抛 `system_error`（[thread.condition.condvar]/2，比如资源不够）。语言的规则在这里兜底：初始化列表里哪个成员没构造完，已经构造好的成员按相反的顺序析构。半成品漏不出去，已构造的部分也不会泄漏，您不用替它操心。

`push` 的路。按值收参，调用者传左值时，拷贝发生在进函数体之前。笔者造了一个拷贝构造从第 3 次起必抛的类型，看看队列被波及了没有：

<OnlineCompilerDemo
  title="拷贝必抛的 FlakyCopy：类型定义在完整实验文件里"
  source-path="code/examples/vol5/chapter03/17_flaky_copy_push.cpp"
  description="拷贝构造从第 3 次起必抛、移动不抛的类型定义，与下一次演示共用同一个文件 flaky_copy_push.cpp：队列与驱动都在里面，这里先认类型本身。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// flaky_copy_push.cpp —— 拷贝构造从第 3 次起必抛的类型：第 3 件左值抛在进 push 函数体之前
// 队列类是 3.3 起三篇搭完的最终形状，原实验放在头文件 queue.hpp 里，这里一并内联
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <queue>
#include <stdexcept>
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

// 拷贝构造从第 3 次起必抛，移动操作不抛
struct FlakyCopy {
  int v;
  inline static int copies = 0;
  explicit FlakyCopy(int x) : v(x) {}
  FlakyCopy(FlakyCopy&&) noexcept = default;
  FlakyCopy& operator=(FlakyCopy&&) noexcept = default;
  FlakyCopy(const FlakyCopy& o) : v(o.v) {
    if (++copies >= 3) throw std::runtime_error("copy #3 爆了");
  }
};

int main() {
  BoundedQueue<FlakyCopy> q(8);
  FlakyCopy a{1}, b{2}, c{3};
  q.push(a);            // 第 1 次拷贝，花在进参数上
  q.push(b);            // 第 2 次
  try {
    q.push(c);          // 第 3 次，抛在进函数体之前
  } catch (const std::runtime_error& e) {
    std::printf("push(c) 抛出：%s\n", e.what());
  }
  std::printf("队列里 %zu 件（应为 2）\n", q.size());
  q.push(FlakyCopy{4}); // 右值：一路移动，不碰拷贝构造
  std::printf("再 push 一件右值，队列里 %zu 件\n", q.size());

  q.close();   // 收工，让下面的排空循环能取完退出
  FlakyCopy out{0};
  while (q.pop(out) == QueueStatus::kOk) {
    std::printf("pop -> %d\n", out.v);
  }
  std::printf("收尾 size = %zu\n", q.size());
  return 0;
}
```

</OnlineCompilerDemo>

驱动它的调用序列不长，您照着摆一遍就能对上输出：

<OnlineCompilerDemo
  title="push(c) 抛在进函数体之前：队列原地不动"
  source-path="code/examples/vol5/chapter03/17_flaky_copy_push.cpp"
  description="前两件左值各花一次拷贝，第 3 件的拷贝在进 push 之前抛出：队列停在 2 件，之后右值照常进队，三件按序取干净。FlakyCopy 与队列都在同一文件里。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// flaky_copy_push.cpp —— 拷贝构造从第 3 次起必抛的类型：第 3 件左值抛在进 push 函数体之前
// 队列类是 3.3 起三篇搭完的最终形状，原实验放在头文件 queue.hpp 里，这里一并内联
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <queue>
#include <stdexcept>
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

// 拷贝构造从第 3 次起必抛，移动操作不抛
struct FlakyCopy {
  int v;
  inline static int copies = 0;
  explicit FlakyCopy(int x) : v(x) {}
  FlakyCopy(FlakyCopy&&) noexcept = default;
  FlakyCopy& operator=(FlakyCopy&&) noexcept = default;
  FlakyCopy(const FlakyCopy& o) : v(o.v) {
    if (++copies >= 3) throw std::runtime_error("copy #3 爆了");
  }
};

int main() {
  BoundedQueue<FlakyCopy> q(8);
  FlakyCopy a{1}, b{2}, c{3};
  q.push(a);            // 第 1 次拷贝，花在进参数上
  q.push(b);            // 第 2 次
  try {
    q.push(c);          // 第 3 次，抛在进函数体之前
  } catch (const std::runtime_error& e) {
    std::printf("push(c) 抛出：%s\n", e.what());
  }
  std::printf("队列里 %zu 件（应为 2）\n", q.size());
  q.push(FlakyCopy{4}); // 右值：一路移动，不碰拷贝构造
  std::printf("再 push 一件右值，队列里 %zu 件\n", q.size());

  q.close();   // 收工，让下面的排空循环能取完退出
  FlakyCopy out{0};
  while (q.pop(out) == QueueStatus::kOk) {
    std::printf("pop -> %d\n", out.v);
  }
  std::printf("收尾 size = %zu\n", q.size());
  return 0;
}
```

</OnlineCompilerDemo>

前两件以左值 push，各花掉一次拷贝，花在进参数的动作上。第 3 件的拷贝在进函数体之前抛出，咱们看输出：

```text
push(c) 抛出：copy #3 爆了
队列里 2 件（应为 2）
再 push 一件右值，队列里 3 件
pop -> 1
pop -> 2
pop -> 4
收尾 size = 0
```

队列原地不动，尺寸还是 2，之后右值 push（一路移动，不碰拷贝构造）照常进队，三件货按次序取干净。异常从哪里进来，队列就停在哪儿，随后照常干活。

进了函数体之后，承诺来自标准，咱们逐句念。`queue::push` 委托给底层容器的 `push_back`（[queue.defn]/1），而容器要求里写着，往尾巴放元素的函数抛了异常就当没发生（[container.reqmts]/66.2、[deque.modifiers]/3）。队列保持原样，锁由 `unique_lock` 的析构放掉，被跳过的 `notify_one` 不欠任何人，条件没变，本来就没有谁该被叫醒。承诺有一个例外：T 只能移动、而移动构造会抛的时候，效果未指明（[deque.modifiers]/3 的最后一句）。例外怎么拦？下面用 `static_assert` 落地。

`pop` 的路更险。`out = std::move(queue_.front())` 把货从队头搬出来，之后才 `queue_.pop()`。移动构造要是抛了呢？元素还躺在队列里，调用者没拿到，队列也没删，下次 pop 遇上的还是它。笔者用一个从第 2 次移动起必抛的类型跑了实证。为了让闸不拦路，笔者另搭了一个副本装它，起名 NaiveQueue，形状就是《有容量的队列：先让它站得住》开头那版教学版，`pop` 还是按值返回的旧签名：

<OnlineCompilerDemo
  title="移动必抛的 Fragile：元素永远卡在队头"
  source-path="code/examples/vol5/chapter03/18_fragile_move_pop.cpp"
  description="入队靠省略少走一次移动，从队头搬出的那次必抛：元素卡在队里，每回 pop 在同一处摔倒。装它的是教学版形状的 NaiveQueue，pop 仍是按值返回的旧签名。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// fragile_move_pop.cpp —— 移动构造从第 2 次起必抛的类型：元素永远卡在队头，每回 pop 同一处摔倒
// NaiveQueue 是 3.3 第 1 节教学版的副本，pop 按值返回的旧签名，专门装这个类型
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
```

</OnlineCompilerDemo>

push 能成功，因为 C++17 的省略规则让入参少走一次移动，进容器的是第 1 次。从队列往外搬是第 2 次，咱们看它撑不撑得住：

```text
push 成功，队列里 1 件
pop 抛出：第 2 次移动起，一搬就爆
队列里 1 件（元素还卡着）
再 pop 又抛：第 2 次移动起，一搬就爆
队列里 1 件
```

剩下的 1 件永远取不出来，每回 pop 都在同一处摔倒。标准对它不给承诺，咱们自己给：把 T 的移动构造与移动赋值都要求不抛，写进 `static_assert`，编译期就把不合格的类型拦下：

<OnlineCompilerDemo
  title="static_assert 两道闸：会抛的移动编译期就拦下"
  source-path="code/examples/vol5/chapter03/19_queue_static_assert.cpp"
  description="正文的两条 static_assert 收进完整队列类：移动构造、移动赋值都得承诺不抛。int 过闸跑一遍；BrokenMove 那次编译失败留在正文的输出块里。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// queue_static_assert.cpp —— 两道编译期的闸：移动构造、移动赋值会抛的类型，实例化时就拦下
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <queue>
#include <type_traits>
#include <utility>

enum class QueueStatus {
  kOk,
  kClosed,
};

template <typename T>
class BoundedQueue {
  static_assert(std::is_nothrow_move_constructible_v<T>,
                "T must be nothrow move constructible");
  static_assert(std::is_nothrow_move_assignable_v<T>,
                "T must be nothrow move assignable");

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

int main() {
  BoundedQueue<int> q(4);   // int 移动构造、移动赋值都不抛，两道闸都过得去
  q.push(7);
  int v = 0;
  q.pop(v);
  std::printf("int 过闸：push 7，pop -> %d\n", v);
  return 0;
}
```

</OnlineCompilerDemo>

第二道拦的是另一条缝：您复用 `out` 连续取件时，赋值要是抛了，元素同样卡在队头。咱们拿一个能移动但不承诺不抛的类型，起名 BrokenMove，单独喂给编译器一个最小翻译单元，f3_static_fail.cpp，内容只有类型定义加一行实例化：

```text
queue.hpp: In instantiation of ‘class BoundedQueue<BrokenMove>’:
f3_static_fail.cpp:11:29:   required from here
queue.hpp:16:22: error: static assertion failed: T must be nothrow move constructible
   16 |   static_assert(std::is_nothrow_move_constructible_v<T>,
      |                 ~~~~~^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
```

拦下它的是第一道闸，连移动构造都不承诺不抛，还没轮到第二道出场。拦截发生在编译期。`int`、`std::string`、`std::unique_ptr` 等常用类型都过得去。真有类型过不了闸，您别怪队列苛刻，它自己的移动操作确实还欠着不抛的承诺。

析构。队列对象销毁时，剩下的元素逐个析构。栈展开进行到一半时，析构函数没有抛异常的退路：再抛新的，`std::terminate` 直接接手（[except.terminate]/1.4）。所以您给 T 的纪律只有一条，析构不抛。

锁的释放是全场兜底。每个方法里的锁都由 RAII 对象持有，正常走释放，异常走也释放。`wait` 内部锁带出去、带回来的交接是标准保证的原子动作，3.1 讲过。异常路径上真正要您操心的，只有两处搬运。

## 轮到您动手的时候

动笔写队列，或者接手别人的队列，笔者会把几样问题过一遍：

1. 两个方向的谓词各是什么？改条件的人拿的锁，与等条件的人拿的锁，是同一把吗？
2. 您的队列打算给几种背压？被丢掉的每一件，调用者知情吗，知情之后能做什么？
3. `close()` 之后谁等消费者退完？队列对象出作用域之前，还有没有人睡在它的条件变量上？
4. T 的移动构造、移动赋值、析构，三样敢不敢都不抛？有一样不敢，队列会在哪一步漏？
5. 消费者加工一件要多久？容量按消费一件的耗时乘上您能接受的等待来估，比拍脑袋靠谱。

## 参考资源

各实验原本跑在头文件 `queue.hpp` 上，它的最终形状已内联进示例文件。cppreference 本回抓到了 `std::queue` 与 `std::condition_variable` 两页全文，与草案核对无出入。推进顺序参照了 Anthony Williams 的《C++ Concurrency in Action》，例子与文字都是笔者自己写的。

- [std::condition_variable（cppreference）][cppref-cv]，C++11 起。
- [std::queue（cppreference）][cppref-queue]，容器适配器，默认底层容器是 `deque`。
- [Class condition_variable（现行草案 thread.condition.condvar）][draft-condvar]，`notify_one` 与 `notify_all` 的效果、解除阻塞的来路、谓词版的等价展开、构造函数可抛 `system_error`、析构的前置条件。
- [Class template queue（现行草案 queue.defn）][draft-queue]，`push` 委托给底层容器的 `push_back`。
- [Modifiers（现行草案 deque.modifiers）][draft-deque-modifiers]，尾端插入的异常承诺，与“只能移动的 T 移动构造抛出时效果未指明”的例外。
- [Container requirements（现行草案 container.reqmts）][draft-creq]，`push_back` 类操作抛了无效果、`pop` 类操作不抛的总要求。
- [The std::terminate function（现行草案 except.terminate）][draft-terminate]，栈展开中析构抛出即终止。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，推进顺序参考。

[cppref-cv]: https://en.cppreference.com/w/cpp/thread/condition_variable
[cppref-queue]: https://en.cppreference.com/w/cpp/container/queue
[draft-condvar]: https://eel.is/c++draft/thread.condition.condvar
[draft-queue]: https://eel.is/c++draft/queue.defn
[draft-deque-modifiers]: https://eel.is/c++draft/deque.modifiers
[draft-creq]: https://eel.is/c++draft/container.reqmts
[draft-terminate]: https://eel.is/c++draft/except.terminate
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
