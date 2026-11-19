---
title: "一次操作的边界在哪里"
description: "把锁放到一次完整操作的边界上：看清检查与动手之间的窗口、跨两个对象的不变量，以及引用、迭代器和回调怎样把内部状态漏给调用者。"
chapter: 5
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 一次操作的边界在哪里

上一篇《用互斥保护共享状态》把 `std::mutex` 和 RAII 锁交到咱们手里，也说了每把锁保护哪份数据。这一篇问的是同一件事的另一半：一次操作由哪几步组成，锁的边界要盖到哪一步才算盖全。

咱们没法用代码行数来量锁的边界。这次操作向调用者承诺了什么结果，锁就得盖住产生承诺结果所需的全部步骤和数据。承诺之外的事，不必塞进临界区。这一篇看三种把边界划错的地方：检查与动手分开，一条不变量跨了两个对象（一次操作过程中任何时刻都得成立的关系），接口把内部状态交了出去。

文中程序都在临时目录里编译跑过（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，WSL2，16 逻辑核，2026-11-14）。有两处改用 `-O1 -fsanitize=thread`（ThreadSanitizer，简称 TSan）编译，另有一处用地址检测器编译。它们能报出普通运行看不见的竞争和内存错误，您会看到。咱们把标准条款按 eel.is 上的现行草案与 cppreference 核对过，链接都收在参考资源里了。文中的代码片段只留关键部分，完整程序补上对应头文件就能编译运行。`std::mutex` 和 `std::lock_guard` 是 C++11 起的设施，`std::scoped_lock` 是 C++17 起，版本标注咱们沿用上一篇。输出都是单次运行的读数，换台机器，数字会动。

## 检查通过了，动手的时候条件已经变了

咱们看一种最常见的空档：检查与动手分成两次调用。讲 data race 与 race condition 的那一篇，是《从错误程序认识竞争》（2.1）。里面的计数器问题出在读改写：三步各自都完整，合起来却不完整。每一步都加锁，只修了“每一步各自完整”，没修“三步连在一起”。

我在实验里摆了一个货架，货架上只剩一件货了，偏偏两条线程同时来买：

```cpp
struct Shelf {
  std::mutex m;
  int stock = 1;

  bool has_one() {
    std::lock_guard<std::mutex> lock(m);
    return stock > 0;
  }
  void take_one() {
    std::lock_guard<std::mutex> lock(m);
    --stock;
  }
};
```

```cpp
using namespace std::chrono_literals;

void buy(const char* who) {
  if (!shelf.has_one()) {
    std::printf("%s: sold out\n", who);
    return;
  }
  std::printf("%s: sees stock = 1\n", who);
  std::this_thread::sleep_for(50ms);   // 把检查与动手之间的窗口放大
  shelf.take_one();
  std::printf("%s: takes one\n", who);
}
```

`sleep_for` 是我加的，只为把窗口放大。真实的程序里，这段窗口由调度和两条线程各自的执行时间凑出来，不需要谁真的睡 50 毫秒。咱们跑了三轮，结果都一样：库存 -1、卖出 2。打印顺序由调度定，可能换过来。

```text
round 1: buyer A checks, sees stock = 1
round 1: buyer B checks, sees stock = 1
round 1: buyer A takes one
round 1: buyer B takes one
round 1: stock left -1, sold 2
```

您看他们，检查的时候都看到 1，检查完都放锁，中间谁也没挡住谁。`take_one()` 各拿一次锁，各减一次，减到 -1：一件货卖给了两位买家。

锁本身没有坏，可边界划错了。`has_one()` 和 `take_one()` 各自都加了锁，临界区里的访问也确实互斥，问题出在两次临界区之间。咱们把一次操作切成了两个临界区，中间那一段是敞开的。互斥量保证同一时刻只有一个线程在临界区里，它不保证您走出临界区之后，状态还停在您看见的样子。

咱们只需要改一处：让锁盖住整次操作。

```cpp
// Shelf 的成员函数
bool try_take() {
  std::lock_guard<std::mutex> lock(m);
  if (stock == 0) return false;
  --stock;
  return true;
}
```

咱们把 `buy()` 里的 `has_one()` 和 `take_one()` 换成了一次 `try_take()`，判断和扣减就在同一个临界区里完成了。我跑了三轮，结果都是：

```text
round 1: buyer A takes the last one
round 1: buyer B finds none left
round 1: stock left 0, sold 1
```

咱们只认结果。至于是谁抢到货，也可能换。不变的是，只有一个人拿到货。

咱们管这类毛病叫 check-then-act，检查之后动手，讲竞争的那一篇提过名字。形状不止货架。那一篇的库存例子也是同一类问题：查的时候还有，扣的时候已经被别人买走了。`size()` 看一眼再 `push_back`，`find()` 找到再 `erase()`，中间都隔着一次解锁和一次加锁，别的线程就能进来。

还有一层容易漏。如果接口把检查单独做成 `has_one()` 交出去，调用者拿它决定下一步，那您把临界区切在哪儿都没用，决策发生在锁外面。答案是在锁里读到的，可您拿它做决定的时候，锁已经放了，状态可能已经变了。`has_one()` 可以留着，给日志、给界面显示、给“大概还有没有”这类参考。但凡调用者要拿它去改状态，接口就该提供一次调用完成判断和修改的操作，比如 `try_take()`。

`std::atomic` 也不解决这件事，那一篇的原子计数器就是证据：每次读写都有定义，连起来还是丢。原子只管单次动作，复合操作要的是把动作连起来。咱们要的“连”，得靠锁，或者靠一次原子读改写，比如 `fetch_add`。后者归讲原子操作与内存模型的那几篇。

## 一条不变量横跨两个对象

另一种边界划错，出在一条跨了两个对象的关系上。我拿两个钱包做例子（`Wallet`，分锁的版本），A 里有 700，B 里有 300，两边加起来 1000。操作是从 A 挪 10 到 B。要求很朴素：任何时刻两边加起来都得是 1000。

```cpp
using namespace std::chrono_literals;

struct Wallet {
  std::mutex m;
  long balance;
};

void move(Wallet& from, Wallet& to, long amount) {
  {
    std::lock_guard<std::mutex> lock(from.m);
    from.balance -= amount;
  }
  std::this_thread::sleep_for(1ms);   // 把两步之间的窗口放大
  {
    std::lock_guard<std::mutex> lock(to.m);
    to.balance += amount;
  }
  std::this_thread::sleep_for(1ms);   // 让完整状态也占一段窗口
}
```

每一步都锁了，每个钱包的余额也从来没被两条线程同时碰过。可这次操作有两步，两步之间放锁，别的线程就能看见“A 少了 10，B 还没多”。我加了一条观察线程，它按同样的办法读两边：

```cpp
long total(Wallet& a, Wallet& b) {
  long sum = 0;
  {
    std::lock_guard<std::mutex> lock(a.m);
    sum += a.balance;
  }
  {
    std::lock_guard<std::mutex> lock(b.m);
    sum += b.balance;
  }
  return sum;
}
```

观察线程读两边的时候，`move()` 也可能正停在两步中间。我让 `move()` 两步之间睡 1 毫秒，挪完再睡 1 毫秒，让两种状态各占一段时间。每一回挪 40 次，跑两回。观察线程从挪动开始跟着采，最多 100 毫秒，挪动结束就停。下面是第一回的前四个坏采样和两回的汇总：

```text
round 1 sample 623: total 990
round 1 sample 624: total 990
round 1 sample 625: total 990
round 1 sample 626: total 990
round 1: per-object locks: 1277732 of 2580701 samples saw a total other than 1000
round 1 final: A = 300, B = 700, total = 1000
round 2: per-object locks: 1221448 of 2470174 samples saw a total other than 1000
round 2 final: A = 300, B = 700, total = 1000
```

990 是 A 已经减了 10、B 还没加上的那一刻。它每次只停 1 毫秒，观察线程采得密，两回都有近一半的采样落在里面。您可能觉得观察线程太闲，现实里读这层关系的人不少：报表、额度检查、另一个方向的挪动，都可能在中间读。

咱们把修法收在一处：把不变量放进同一把锁。不变量说的是 A 和 B 的和，那就让一把锁盖住 A 和 B。换成 `Ledger`（合锁的版本）：

```cpp
struct Ledger {
  std::mutex m;
  long a = 700;
  long b = 300;

  void transfer(long amount) {
    std::lock_guard<std::mutex> lock(m);
    a -= amount;
    b += amount;
  }
  long total() {
    std::lock_guard<std::mutex> lock(m);
    return a + b;
  }
};
```

`transfer()` 里两次修改在同一个临界区里，`total()` 也用同一把锁。咱们用同一套采样窗口再跑两回。实验里的调用方在每次 `transfer()` 之后睡 1 毫秒，和 Wallet 版的节奏对齐，零次读到 990：

```text
round 1: one lock for both sides: 0 of 1524458 samples saw a total other than 1000
round 1 final: A = 300, B = 700, total = 1000
round 2: one lock for both sides: 0 of 1511547 samples saw a total other than 1000
round 2 final: A = 300, B = 700, total = 1000
```

关系提到两份数据，只锁其中一份，另一份就在保护之外。观察者也得用同一把锁，不然它仍然会把两边分开读。咱们写接口的时候，常按对象分锁，每个钱包一把。不变量跨过两把锁，就没有任何一把锁在管。

一把锁盖住所有钱包，代价是所有挪动都排队，哪怕两边根本不相干。咱们要按钱包分锁，就得在一次挪动里同时拿住两把，并且固定拿锁顺序，防止两边互相等。上一篇讲过的 C++17 `std::scoped_lock` 可以一次接收几把锁，拿法交给标准库的死锁避免算法。拿锁顺序和死锁预防归《减少共享与避免死锁》，咱们本篇只把边界说清：不变量需要两把锁一起在场，缺一把都不成立。

> 咱们实验里的 `Shelf`、`Wallet` 和 `Ledger` 都把字段露在外面。真正写接口的时候，字段和锁要一起收进 private，上一篇的 `Counter` 就是这么做的。后面讲接口的部分接着讲封装之后还会从哪里漏。

## 接口交出去的东西，也是边界的一部分

钱包的例子收在锁和不变量上。接口这边还有一条路：锁没被绕开，调用者却拿到了内部状态。上一篇的 `Box` 让咱们看见一条漏出去的引用，例子是 `int&`。这一篇看四种交法：引用、`const&`、迭代器、回调参数。它们交出去的都是位置或引用，调用者拿到以后都在锁外面。

我另写了一个 `Gauge`（仪表），`set()` 和 `value()` 都加锁，只把 `value()` 的返回类型写成 `const int&`：

```cpp
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
```

`value()` 里确实加了锁，可锁在函数返回时就放了。调用者拿到的引用，读发生在锁外面。咱们让主线程拿着引用读，另一条线程不停 `set()`：

```cpp
Gauge gauge;
std::atomic<bool> done{false};
std::thread writer([&] {
  for (int i = 1; i <= 100000; ++i) gauge.set(i);
  done.store(true);
});

long long sum = 0;
while (!done.load()) sum += gauge.value();
writer.join();
```

咱们用 TSan 编一版，一回跑出来的报告是：

```text
WARNING: ThreadSanitizer: data race (pid=37)
  Write of size 4 at 0x7fffffffd3d8 by thread T1 (mutexes: write M0):
    #0 ... main::{lambda()#1} ... (gauge_lock+0x1372)
  Previous read of size 4 at 0x7fffffffd3d8 by main thread:
    #0 main ... (gauge_lock+0x1601)
  Location is stack of main thread.
```

报告里哪一行排在前头会换。咱们只看两点：写的一方拿着锁，读的一方没拿。地址和调用栈我删了几行。`const` 只约束调用者那一侧，管不了另一条线程在同一时刻写。读和写之间没有 happens-before，按讲竞争的那一篇的定义，这就是 data race。

咱们把返回值改成按值的：

```cpp
int value() const {
  std::lock_guard<std::mutex> lock(m_);
  return value_;
}
```

我换回按值返回，再编一次，TSan 不再报。读循环读到的每个数，都是某个完整时刻的值，代价是每次读都要进一次临界区，大对象还得复制一份。

迭代器还会交出位置。咱们看一个带 `begin()` 的容器：

```cpp
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
```

调用者拿到迭代器以后，锁早就放了。咱们单线程跑一遍也能踩中：

```cpp
History history;
history.add(1);
history.add(2);
auto it = history.begin();   // 锁在 begin() 返回时放了
history.add(3);              // 容量不够，重新分配，迭代器失效
int x = *it;                 // 读的是已经释放的内存
```

`push_back` 触发重新分配时，所有指向元素的引用、指针和迭代器全部失效（[vector.modifiers]/2，现行草案）。我用 AddressSanitizer（ASan）编了一次，选项是 `-O1 -fsanitize=address`：

```text
ERROR: AddressSanitizer: heap-use-after-free
READ of size 4 ... thread T0
    #0 ... main ...
```

多线程还多一层。咱们分开看。没有重新分配、也没有别人写同一个元素时，读 `*it` 本身不冲突。另一个线程一旦写同一个元素，锁外的读和锁里的写之间没有 happens-before，TSan 会报 data race：

```cpp
History history;
history.add(10);
history.add(20);
auto it = history.begin();   // 锁在 begin() 返回时放了
std::atomic<bool> done{false};
// 另一条线程不停 history.set(0, i)，结束时把 done 置真
long long sum = 0;
while (!done) sum += *it;    // 锁外的读
```

```text
WARNING: ThreadSanitizer: data race (pid=21)
  Write of size 4 ... by thread T1 (mutexes: write M0):
    #0 ... main::{lambda()#1} ...
  Previous read of size 4 ... by main thread:
    #0 main ...
```

咱们看报告里的两方。写的一方拿着 `set()` 的锁，报告里的 M0 就是它。读的一方没有。调用者手里只有一个位置，他没法知道别的线程什么时候改它、什么时候再 `push_back` 一下。拿 `it` 和 `end()` 比，有两种情形。`it` 已经失效时，您是在拿一个不属于您的位置做比较。`it` 还没失效、另一条线程正在 `push_back` 时，`end()` 虽然在锁里取，取到的末尾位置在锁放开以后也可能被下一次 `push_back` 改掉，比较同样没有可靠依据。

咱们要交，就交一份副本。锁里把容器复制出来，锁外随便用：

```cpp
std::vector<int> snapshot() const {
  std::lock_guard<std::mutex> lock(m_);
  return items_;
}
```

在 `History` 的接口上，副本有代价，元素多的时候拷贝不便宜。咱们得认下这样的代价：要么让调用者拿副本在锁外慢慢看，要么在锁里把要用的结果算完再交出去。大的对象，还可以返回指向快照副本的 `shared_ptr`，或者干脆不交数据、只交操作。

咱们看回调，它是同一类出口的另一种形式。锁里调用调用者给的函数，调用者那段代码就在锁里跑，还能拿到内部元素的引用：

```cpp
template <class F>
void for_each(F f) {              // 回调在锁里跑
  std::lock_guard<std::mutex> lock(m_);
  for (auto& item : items_) f(item);
}
```

回调收到的是 `int&`，它可以把引用存进自己的容器。咱们把锁一放，引用还在，回调什么时候用就管不着了。修法还是那句，锁里把要交出去的数据取出来，出了临界区再把副本交给回调：

```cpp
template <class F>
void for_each(F f) const {
  std::vector<int> copy;
  {
    std::lock_guard<std::mutex> lock(m_);
    copy = items_;
  }
  for (const auto& item : copy) f(item);
}
```

咱们把副本交给回调，回调的签名也跟着改成只读值或只读引用。副本在 `for_each` 活着的时候有效。回调要把引用留到调用之后，得自己复制一份，这一层约定得写进接口说明里。

回调在锁里再调本类的成员函数，可能死锁（[thread.mutex.class]/4，C++11 起），上一篇讲粒度时提过。我在 GCC 16.2.1 上试了一次：同一条线程第二次锁同一把 `std::mutex`，进程直接卡住，`timeout` 2 秒后以 124 退出。回调有两处限制：不在锁里调它，不让它拿到的引用活到调用之后。两处都做到，回调才不留下这两类问题。

咱们判断一个接口有没有留后门，问法很直接：调用者拿到返回值以后，还需要不需要这把锁？如果需要，类就没把保护做完。引用、`const&`、迭代器、回调参数，都得过这一问。`const` 成员也一样，只读的访问也要进临界区。

咱们按上一篇的两条来：锁里别塞耗时活，别调不受控的外部代码。

## 把边界划在操作上

货架、钱包、仪表看下来，落笔的时候，我习惯把这几件事过一遍：

1. 这次操作，您从哪里算起点，到哪里算终点？调用者看到的是一步，还是几步？中间放锁，别的线程能看见什么？货架的答案是“检查与扣减”算一步，`Wallet` 的答案是“从 A 挪到 B”算一步。`Gauge` 那边，操作本来就是一步，问题出在接口交出去的位置。
2. 这次操作依赖哪条关系？您数一数关系提到几份数据，它们是不是在同一把锁底下？`Wallet`（分锁）里两个余额各有一把锁，不变量跨过去就没人管。`Ledger`（合锁）里一把锁盖住两个。
3. 接口交出去的是什么？您把引用、`const&`、迭代器、回调参数过一遍，会不会让调用者在锁外面接着动？`Gauge` 交出了 `const int&`，调用者就不用进临界区了。

## 下一步

咱们本单元的《减少共享与避免死锁》，会接着讲锁顺序和减少共享。一次操作如果非得同时拿两把锁，拿的顺序就会变成正确性的一部分：两条线程按相反顺序拿两把锁（AB-BA），就会各自等对方手里那把。反过来，如果两边根本不需要共享，把数据分开、各拿各的，锁和边界一起消失。那一篇从 AB-BA 反转讲起，再回到独立持有、传递结果、共同修改三种方案的取舍。

本篇的边界是那一篇的前提：您得知道一次操作要盖住哪些数据，才谈得上按什么顺序拿锁。

在您离开这一篇之前，我请您做一件事：找一段您自己写过的“查一下，再改一下”的代码，看看查询接口是不是把答案交给了调用者。能合成一次调用，就合成。合成不了，把不能合成的理由写下来。那通常就是后面那一篇要处理的多把锁问题。

## 参考资源

本篇写作时对照借鉴了 Anthony Williams 的《C++ Concurrency in Action》（Manning 第二版），check-then-act 与并发接口的讲法，参考了它讲共享数据的那几章。例子与文字都是我自己写的，实验在 GCC 16.2.1 上编译实测。涉及标准行为的断言，按文中标注的条款核对过 eel.is 上的草案文本与 cppreference。

- [std::mutex（cppreference）][cppref-mutex]，互斥量的接口与不可重入的说明，C++11 起。
- [std::lock_guard（cppreference）][cppref-lockguard]，构造时加锁、析构时解锁，C++11 起。
- [Mutex requirements（现行草案 thread.mutex.requirements.mutex）][draft-mutex-req]，`lock()` 与 `unlock()` 的前置条件和同步关系。
- [Class mutex（现行草案 thread.mutex.class）][draft-mutex-class]，不可重入，同一线程重复加锁可能死锁（草案注）。
- [Data races（现行草案 intro.races）][draft-races]，data race 的定义与未定义行为。
- [vector 的修改操作（现行草案 vector.modifiers）][draft-vector-modifiers]，`push_back` 重新分配时迭代器与引用的失效规则。
- [Multi-threaded executions and data races（cppreference）][cppref-multithread]，happens-before 与同步操作的说明。
- [C++ Core Guidelines CP.50][cp50]，把互斥量和它保护的数据放在一起。
- [ThreadSanitizer（Google Sanitizers wiki）][tsan-wiki]，报告格式与检测范围。

[cppref-mutex]: https://en.cppreference.com/w/cpp/thread/mutex
[cppref-lockguard]: https://en.cppreference.com/w/cpp/thread/lock_guard
[draft-mutex-req]: https://eel.is/c++draft/thread.mutex.requirements.mutex
[draft-mutex-class]: https://eel.is/c++draft/thread.mutex.class
[draft-races]: https://eel.is/c++draft/intro.races
[draft-vector-modifiers]: https://eel.is/c++draft/vector.modifiers
[cppref-multithread]: https://en.cppreference.com/w/cpp/language/multithread
[cp50]: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp50-define-a-mutex-together-with-the-data-it-guards-use-synchronized_valuet-where-possible
[tsan-wiki]: https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual
