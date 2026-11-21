---
title: "边界交给了调用者：接口的四种漏法"
description: "锁没被绕开，调用者却拿到了内部状态：引用、const 引用、迭代器、回调参数四种交法各摆一个现场，末了把边界划在操作上。"
chapter: 5
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 边界交给了调用者：接口的四种漏法

三种情形走到第三处，也是最隐蔽的一处：接口把内部状态交了出去。锁没被绕开，调用者却拿到了内部状态。《锁盖多大才算够》里的 `Box` 让咱们看见一条漏出去的引用，例子是 `int&`。这一篇看四种交法：引用、`const&`、迭代器、回调参数。它们交出去的都是位置或引用，调用者拿到以后都在锁外面。

笔者另写了一个 `Gauge`（仪表），`set()` 和 `value()` 都加锁，只把 `value()` 的返回类型写成 `const int&`：

<OnlineCompilerDemo
  title="Gauge 坏版：value() 返回 const int&"
  source-path="code/examples/vol5/chapter02/22_demo_gauge_const_ref.cpp"
  description="set 与 value 都加了锁，可锁在函数返回时就放了：主线程拿着引用在锁外读，写线程在锁里写。在线组件跑的是无检测器版，TSan 报告需按正文给的参数（-O1 -fsanitize=thread）本地复现。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_gauge_const_ref.cpp —— const int& 交出去：锁放了，读还在锁外
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

class Gauge {
 public:
  void set(int v) {
    std::lock_guard<std::mutex> lock(m_);
    value_ = v;
  }
  const int& value() const {
    std::lock_guard<std::mutex> lock(m_);
    return value_;
  }

 private:
  mutable std::mutex m_;
  int value_ = 0;
};

int main() {
  Gauge gauge;
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (int i = 1; i <= 100000; ++i) gauge.set(i);
    done.store(true);
  });

  long long sum = 0;
  while (!done.load()) sum += gauge.value();
  writer.join();
  std::printf("sum = %lld (with -fsanitize=thread this reports a data race)\n", sum);
  return 0;
}
```

</OnlineCompilerDemo>

`value()` 里确实加了锁，可锁在函数返回时就放了。引用交到调用者手里，读就发生在锁外面了。咱们让主线程拿着引用读，另一条线程不停 `set()`：

<OnlineCompilerDemo
  title="读引用的循环：另一条线程不停 set()"
  source-path="code/examples/vol5/chapter02/22_demo_gauge_const_ref.cpp"
  description="同一个程序：写线程十万个 set，主线程的 while 循环拿着 value() 交出来的引用在锁外累加。TSan 报告里写的一方拿着锁（M0），读的一方没有。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_gauge_const_ref.cpp —— const int& 交出去：锁放了，读还在锁外
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

class Gauge {
 public:
  void set(int v) {
    std::lock_guard<std::mutex> lock(m_);
    value_ = v;
  }
  const int& value() const {
    std::lock_guard<std::mutex> lock(m_);
    return value_;
  }

 private:
  mutable std::mutex m_;
  int value_ = 0;
};

int main() {
  Gauge gauge;
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (int i = 1; i <= 100000; ++i) gauge.set(i);
    done.store(true);
  });

  long long sum = 0;
  while (!done.load()) sum += gauge.value();
  writer.join();
  std::printf("sum = %lld (with -fsanitize=thread this reports a data race)\n", sum);
  return 0;
}
```

</OnlineCompilerDemo>

有三处改用 `-O1 -fsanitize=thread`（ThreadSanitizer，简称 TSan）编译，另有一处前半用地址检测器编译、后半用 TSan。它们能报出普通运行看不见的竞争和内存错误，往下看您就见到报告了。

咱们用 TSan 编一版，一回跑出来的报告是：

```text
WARNING: ThreadSanitizer: data race (pid=37)
  Write of size 4 at 0x7fffffffd3d8 by thread T1 (mutexes: write M0):
    #0 ... main::{lambda()#1} ... (gauge_lock+0x1372)
  Previous read of size 4 at 0x7fffffffd3d8 by main thread:
    #0 main ... (gauge_lock+0x1601)
  Location is stack of main thread.
```

报告里哪一行排在前头会换。咱们只看两点：写的一方拿着锁，读的一方没拿。地址和调用栈笔者删了几行。`const` 只约束调用者那一侧，管不了另一条线程在同一时刻写。读和写之间没有 happens-before，按讲竞争的那一篇的定义，这就是 data race。

咱们把返回值改成按值的：

<OnlineCompilerDemo
  title="Gauge 修版：返回按值，读在锁里完成"
  source-path="code/examples/vol5/chapter02/23_demo_gauge_by_value.cpp"
  description="返回类型从 const int& 改成 int：每次读都进一次临界区，TSan 不再报。代价是大对象每次读都得复制一份。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_gauge_by_value.cpp —— 返回按值：每次读都在锁里完成
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

class Gauge {
 public:
  void set(int v) {
    std::lock_guard<std::mutex> lock(m_);
    value_ = v;
  }
  int value() const {
    std::lock_guard<std::mutex> lock(m_);
    return value_;
  }

 private:
  mutable std::mutex m_;
  int value_ = 0;
};

int main() {
  Gauge gauge;
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (int i = 1; i <= 100000; ++i) gauge.set(i);
    done.store(true);
  });

  long long sum = 0;
  while (!done.load()) sum += gauge.value();
  writer.join();
  std::printf("sum = %lld (TSan compiles clean, every read inside the lock)\n", sum);
  return 0;
}
```

</OnlineCompilerDemo>

笔者换回按值返回，再编一次，TSan 不再报。读循环拿到的每个数，都是某个完整时刻的值，代价是每次读都要进一次临界区，大对象还得复制一份。

迭代器还会交出位置。咱们看一个带 `begin()` 的容器：

<OnlineCompilerDemo
  title="History：begin() 交出去的是位置"
  source-path="code/examples/vol5/chapter02/24_demo_history_iterators.cpp"
  description="add 与 begin 都加了锁，可调用者拿到迭代器以后锁早就放了：单线程加一个元素就能让 begin() 拿到的迭代器失效，多线程读它再碰上写同一个元素就是 data race。在线组件跑的是无检测器版，ASan、TSan 的报告需按正文给的参数本地复现。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_history_iterators.cpp —— 迭代器交出去的是位置：单线程也踩得中
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

class History {
 public:
  void add(int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_.push_back(v);
  }
  void set(std::size_t i, int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_[i] = v;
  }
  std::vector<int>::iterator begin() {
    std::lock_guard<std::mutex> lock(m_);
    return items_.begin();
  }
  std::vector<int>::iterator end() {
    std::lock_guard<std::mutex> lock(m_);
    return items_.end();
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  history.add(1);
  history.add(2);
  auto it = history.begin();   // 锁在 begin() 返回时放了
  history.add(3);              // 容量不够，重新分配，迭代器失效
  int x = *it;                 // 读的是已经释放的内存（ASan 下报 heap-use-after-free）
  std::printf("dangling iterator read: %d\n", x);

  History raced;
  raced.add(10);
  raced.add(20);
  auto rit = raced.begin();    // 锁在 begin() 返回时放了
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (int i = 1; i <= 100000; ++i) raced.set(0, i);
    done.store(true);
  });
  long long sum = 0;
  while (!done.load()) sum += *rit;   // 锁外的读（TSan 下报 data race）
  writer.join();
  std::printf("out-of-lock reads summed to %lld\n", sum);
  return 0;
}
```

</OnlineCompilerDemo>

调用者拿到迭代器以后，锁早就放了。咱们单线程跑一遍也能踩中：

<OnlineCompilerDemo
  title="单线程踩中失效迭代器，多线程踩中 data race"
  source-path="code/examples/vol5/chapter02/24_demo_history_iterators.cpp"
  description="前半段单线程：begin() 之后 add(3) 触发重新分配，it 读已释放的内存。后半段多线程：另一条线程不停 set(0, i)，主线程在锁外读 *rit。两种检测器各报各的。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_history_iterators.cpp —— 迭代器交出去的是位置：单线程也踩得中
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

class History {
 public:
  void add(int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_.push_back(v);
  }
  void set(std::size_t i, int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_[i] = v;
  }
  std::vector<int>::iterator begin() {
    std::lock_guard<std::mutex> lock(m_);
    return items_.begin();
  }
  std::vector<int>::iterator end() {
    std::lock_guard<std::mutex> lock(m_);
    return items_.end();
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  history.add(1);
  history.add(2);
  auto it = history.begin();   // 锁在 begin() 返回时放了
  history.add(3);              // 容量不够，重新分配，迭代器失效
  int x = *it;                 // 读的是已经释放的内存（ASan 下报 heap-use-after-free）
  std::printf("dangling iterator read: %d\n", x);

  History raced;
  raced.add(10);
  raced.add(20);
  auto rit = raced.begin();    // 锁在 begin() 返回时放了
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (int i = 1; i <= 100000; ++i) raced.set(0, i);
    done.store(true);
  });
  long long sum = 0;
  while (!done.load()) sum += *rit;   // 锁外的读（TSan 下报 data race）
  writer.join();
  std::printf("out-of-lock reads summed to %lld\n", sum);
  return 0;
}
```

</OnlineCompilerDemo>

`push_back` 触发重新分配时，所有指向元素的引用、指针和迭代器全部失效（[vector.modifiers]/2，现行草案）。笔者用 AddressSanitizer（ASan）编了一次，选项是 `-O1 -fsanitize=address`：

```text
ERROR: AddressSanitizer: heap-use-after-free
READ of size 4 ... thread T0
    #0 ... main ...
```

这次读是未定义行为，ASan 的报告只是一份观察记录，不代表安全。不带检测器裸跑时它多半不报错，打印一个谁也说不清来路的数，这正是它难查的原因。

多线程这边还多一层，咱们接着看。没有重新分配、也没有别人写同一个元素时，读 `*it` 本身不冲突。另一个线程一旦写同一个元素，锁外的读和锁里的写之间没有 happens-before，TSan 会报 data race：

<OnlineCompilerDemo
  title="多线程：锁外的读碰上锁里的写"
  source-path="code/examples/vol5/chapter02/24_demo_history_iterators.cpp"
  description="同一个程序的后半段：写线程拿着 set() 的锁改首元素，主线程拿着 begin() 交出的迭代器在锁外读——TSan 报的写方带 M0、读方两手空空。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_history_iterators.cpp —— 迭代器交出去的是位置：单线程也踩得中
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

class History {
 public:
  void add(int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_.push_back(v);
  }
  void set(std::size_t i, int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_[i] = v;
  }
  std::vector<int>::iterator begin() {
    std::lock_guard<std::mutex> lock(m_);
    return items_.begin();
  }
  std::vector<int>::iterator end() {
    std::lock_guard<std::mutex> lock(m_);
    return items_.end();
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  history.add(1);
  history.add(2);
  auto it = history.begin();   // 锁在 begin() 返回时放了
  history.add(3);              // 容量不够，重新分配，迭代器失效
  int x = *it;                 // 读的是已经释放的内存（ASan 下报 heap-use-after-free）
  std::printf("dangling iterator read: %d\n", x);

  History raced;
  raced.add(10);
  raced.add(20);
  auto rit = raced.begin();    // 锁在 begin() 返回时放了
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (int i = 1; i <= 100000; ++i) raced.set(0, i);
    done.store(true);
  });
  long long sum = 0;
  while (!done.load()) sum += *rit;   // 锁外的读（TSan 下报 data race）
  writer.join();
  std::printf("out-of-lock reads summed to %lld\n", sum);
  return 0;
}
```

</OnlineCompilerDemo>

```text
WARNING: ThreadSanitizer: data race (pid=21)
  Write of size 4 ... by thread T1 (mutexes: write M0):
    #0 ... main::{lambda()#1} ...
  Previous read of size 4 ... by main thread:
    #0 main ...
```

咱们看报告里的两方。写的一方拿着 `set()` 的锁，报告里的 M0 就是它。读的一方没有。调用者手里只有一个位置，他没法知道别的线程什么时候改它、什么时候再 `push_back` 一下。拿 `it` 和 `end()` 比，也一样靠不住。`it` 已经失效时，您是在拿一个不属于您的位置做比较。`it` 还没失效、另一条线程正在 `push_back` 时，`end()` 虽然在锁里取，取到的末尾位置在锁放开以后也可能被下一次 `push_back` 改掉，比较同样没有可靠依据。

咱们要交，就交一份副本。锁里把容器复制出来，锁外随便用：

<OnlineCompilerDemo
  title="要交就交一份副本"
  source-path="code/examples/vol5/chapter02/25_demo_history_snapshot.cpp"
  description="锁里把容器复制出来，锁外随便用：快照之后再 add 也不影响手里的副本。元素多的时候拷贝不便宜，代价得认下。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_history_snapshot.cpp —— 锁里复制一份，锁外随便用
#include <cstdio>
#include <mutex>
#include <vector>

class History {
 public:
  void add(int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_.push_back(v);
  }
  std::vector<int> snapshot() const {
    std::lock_guard<std::mutex> lock(m_);
    return items_;
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  for (int v : {1, 2, 3}) history.add(v);
  std::vector<int> copy = history.snapshot();
  history.add(4);              // 快照之后容器再动，副本不受影响
  long long sum = 0;
  for (int v : copy) sum += v;
  std::printf("snapshot holds %zu items summing to %lld\n", copy.size(), sum);
  return 0;
}
```

</OnlineCompilerDemo>

在 `History` 的接口上，副本有代价，元素多的时候拷贝不便宜。要么让调用者拿副本在锁外慢慢看，要么咱们在锁里把要用的结果算完再交出去。大的对象，还可以返回指向快照副本的 `shared_ptr`，或者干脆不交数据、只交操作。

咱们看回调，它是同一类出口的另一种形式。把调用者给的函数放进锁里调，它就在锁里跑，还能拿到内部元素的引用：

<OnlineCompilerDemo
  title="回调在锁里跑：引用存下来就漏出去了"
  source-path="code/examples/vol5/chapter02/26_demo_callback_in_lock.cpp"
  description="for_each 在锁里调调用者给的函数，回调收到 int&，可以把位置存进自己的容器。锁一放，位置还在：随后 100 次 add 触发重新分配，存下的位置全失效。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_callback_in_lock.cpp —— 回调在锁里跑：引用存下来就漏出去了
#include <cstdio>
#include <mutex>
#include <vector>

class History {
 public:
  void add(int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_.push_back(v);
  }
  template <class F>
  void for_each(F f) {              // 回调在锁里跑
    std::lock_guard<std::mutex> lock(m_);
    for (auto& item : items_) f(item);
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  for (int v : {1, 2, 3}) history.add(v);

  std::vector<int*> kept;           // 回调把内部引用的位置存了下来
  history.for_each([&](int& item) { kept.push_back(&item); });

  for (int i = 0; i < 100; ++i) history.add(0);   // 重新分配后位置全失效
  std::printf("callback kept %zu positions, first one now reads %d\n",
              kept.size(), *kept.front());
  return 0;
}
```

</OnlineCompilerDemo>

回调收到的是 `int&`，它可以把引用存进自己的容器。咱们把锁一放，引用还留在回调手里，它什么时候用、拿来干什么，咱们都管不着了。修法跟前面一样，锁里把要交出去的数据取出来，出了临界区再把副本交给回调：

<OnlineCompilerDemo
  title="锁里取副本，锁外喂给回调"
  source-path="code/examples/vol5/chapter02/27_demo_callback_outside.cpp"
  description="副本在锁里攒好，回调在锁外跑：回调拿到的引用只指向副本，容器之后怎么动都不影响。回调要把引用留到调用之后，得自己复制一份。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_callback_outside.cpp —— 锁里取副本，锁外喂给回调
#include <cstdio>
#include <mutex>
#include <vector>

class History {
 public:
  void add(int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_.push_back(v);
  }
  template <class F>
  void for_each(F f) const {
    std::vector<int> copy;
    {
      std::lock_guard<std::mutex> lock(m_);
      copy = items_;
    }
    for (const auto& item : copy) f(item);
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  for (int v : {1, 2, 3}) history.add(v);

  long long sum = 0;
  history.for_each([&](const int& item) { sum += item; });   // 副本里的引用，随便用
  for (int i = 0; i < 100; ++i) history.add(0);              // 之后再动也不影响
  std::printf("callback summed the snapshot to %lld\n", sum);
  return 0;
}
```

</OnlineCompilerDemo>

咱们把副本交给回调，回调的签名也跟着改成只读值或只读引用。副本在 `for_each` 执行期间有效。回调要把引用留到调用之后，得自己复制一份，这一层约定得写进接口说明里。

回调在锁里再调本类的成员函数，可能死锁（[thread.mutex.class]/4，C++11 起），《锁盖多大才算够》里讲粒度时提过。笔者在 GCC 16.2.1 上试了一次，同一条线程第二次锁同一把 `std::mutex`，进程直接卡住，`timeout` 2 秒后以 124 退出。回调有两处限制：不在锁里调它，不让它拿到的引用活到调用之后。两处都做到，死锁和漏出去的引用才都不会发生。

咱们判断一个接口有没有留后门，问法很直接：调用者拿到返回值以后，用的时候还要不要锁？如果需要，类就没把保护做完。引用、`const&`、迭代器、回调参数，都得过这一问。`const` 成员也一样，只读的访问也要进临界区。

咱们按《锁盖多大才算够》里的两条来：锁里别塞耗时活，别调不受控的外部代码。

## 把边界划在操作上

货架、钱包、仪表看下来，落笔的时候，笔者习惯把几个问题过一遍：

1. 一次操作，您从哪里算起点，到哪里算终点？调用者看到的是一步，还是几步？中间放锁，别的线程能看见什么？货架的答案是“检查与扣减”算一步，`Wallet` 的答案是“从 A 挪到 B”算一步。`Gauge` 那边，操作本来就是一步，问题出在接口交出去的位置。
2. 一次操作依赖哪条关系？您数一数关系提到几份数据，它们是不是在同一把锁底下？`Wallet`（分锁）里两个余额各有一把锁，不变量跨过去就没人管。`Ledger`（合锁）里一把锁盖住两个。
3. 接口交出去的是什么？您把引用、`const&`、迭代器、回调参数过一遍，会不会让调用者在锁外面接着动？`Gauge` 交出了 `const int&`，调用者就不用进临界区了。

## 下一步

在您离开这一篇之前，笔者请您做一件事：找一段您自己写过的“查一下，再改一下”的代码，看看查询接口是不是把答案交给了调用者。能合成一次调用，就合成。合成不了，把不能合成的理由写下来。那通常就是后面那一篇要处理的多把锁问题。


## 参考资源

本篇写作时对照借鉴了 Anthony Williams 的《C++ Concurrency in Action》（Manning 第二版），并发接口的讲法参考了它讲共享数据的几章。例子与文字都是笔者自己写的，实验在 GCC 16.2.1 上编译实测。涉及标准行为的断言，按文中标注的条款核对过 eel.is 上的草案文本与 cppreference。

- [Class mutex（现行草案 thread.mutex.class）][draft-mutex-class]，不可重入，同一线程重复加锁可能死锁（草案注）。
- [vector 的修改操作（现行草案 vector.modifiers）][draft-vector-modifiers]，`push_back` 重新分配时迭代器与引用的失效规则。
- [C++ Core Guidelines CP.50][cp50]，把互斥量和它保护的数据放在一起。

[draft-mutex-class]: https://eel.is/c++draft/thread.mutex.class
[draft-vector-modifiers]: https://eel.is/c++draft/vector.modifiers
[cp50]: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp50-define-a-mutex-together-with-the-data-it-guards-use-synchronized_valuet-where-possible
