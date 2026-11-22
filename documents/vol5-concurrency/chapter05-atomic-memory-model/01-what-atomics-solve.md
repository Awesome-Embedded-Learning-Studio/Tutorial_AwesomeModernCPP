---
title: "原子操作解决到哪一步：类型、操作与 CAS"
description: "std::atomic 认哪些类型、配哪些操作：load/store/fetch_add 的操作面，无锁与内部锁的查询口，CAS 的失败与重试、weak 与 strong 的分工，ABA 的现场与版本号修法。"
chapter: 5
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - atomic
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20, 26]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 原子操作解决到哪一步：类型、操作与 CAS

笔者给自己划的活儿只有一件：把原子操作能担保的与不能担保的画成一条边界。

本篇的 demo 多数是编排好的确定场面，谁读、谁睡、谁动手，次序写死在代码里，您重跑读数也一致。

## std::atomic 认哪些类型，配哪些操作

`std::atomic` 是一个类模板，住在 `<atomic>` 里，C++11 起。咱们把它当一套类型加一套操作来认，类型决定您拿得到哪些操作，操作决定一次动作的形状。分档看最省事。

| 类型档                                                          | 配到的操作                                                                                                  | 版本                              |
| --------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- | --------------------------------- |
| 整型：每种字符类型、每个标准整数类型，含 `<cstdint>` 的 typedef | load、store、exchange、CAS，fetch_add、fetch_sub、fetch_and、fetch_or、fetch_xor，`++`、`--`、`+=` 等运算符 | C++11 起                          |
| 浮点：`float`、`double`、`long double`                          | load、store、exchange、CAS，fetch_add、fetch_sub                                                            | C++20 起                          |
| 指针 `atomic<T*>`                                               | load、store、exchange、CAS，fetch_add、fetch_sub（按元素个数偏移）                                          | C++11 起                          |
| 可平凡复制的自定义类型（主模板）                                | load、store、exchange、CAS，wait、notify                                                                    | C++11 起，wait 与 notify 归 C++20 |
| `atomic<std::shared_ptr<T>>`、`atomic<std::weak_ptr<T>>`        | load、store、exchange、CAS，wait、notify                                                                    | C++20 起                          |

两处容易读岔的地方，咱们交代清楚。`atomic<bool>` 走主模板，标准在注里写明它不归整型特化管（[atomics.types.int] 的注 1），所以 bool 身上没有加法，`fetch_add` 不存在。标准还在往前长：C++26 给整型与指针添了 `fetch_max` 与 `fetch_min`，本卷停在 C++20，您知道有它们就行。

操作面上，`fetch_` 系列把读、算、写当成一次动作做，交回的是运算之前的旧值。咱们跑一段最小的：

<OnlineCompilerDemo
  title="load、exchange、fetch_add 与运算符各交回什么"
  source-path="code/examples/vol5/chapter05/01_demo_ops.cpp"
  description="在一个 atomic<int> 上依次跑 store、exchange、fetch_add、前后置 ++ 与 +=，每个动作打印交回值与当前值。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
store(10) 之后 load      : 10
exchange(3) 交回旧值     : 10，现在 3
fetch_add(5) 交回旧值    : 3，现在 8
counter++ 交回旧值       : 8，现在 9
++counter 交回新值       : 10，现在 10
+= 4 之后                : 14
```

交回旧值是刻意的安排：一次动作同时把读当前状态与改状态都做完，做计数、做配额都顺手。运算符形式另有一副脾气，`++` 前置交回加完的新值，后置交回旧值，跟普通 `int` 的规则一致。`+=` 走的是 fetch_add 的运算符外衣。您要拿返回值参与别的计算，就写显式的 `fetch_add`，意图看得清。

## 原子不等于无锁：is_lock_free 与 is_always_lock_free

到这里笔者一直说“一次动作”，还没交代完整是谁给的。多数人脑子里原子就等于 CPU 指令一条搞定，事实是，类型够小、平台有指令，编译器直接给真指令。类型宽到平台没有对应指令（比如 16 个字节），库退回去用一把内部锁把动作围起来，动作照样完整，代价是它内部真有锁。`std::atomic` 保证的是动作完整，不保证没有锁。

标准库给了两个查询口。`is_always_lock_free` 是编译期常量（C++17 起），true 表示该类型的原子操作在这个平台上永远无锁。`is_lock_free()` 是运行时查询。咱们在本机 x86-64 上跑：

<OnlineCompilerDemo
  title="is_lock_free 与 is_always_lock_free：宽类型不是无锁"
  source-path="code/examples/vol5/chapter05/02_demo_lockfree.cpp"
  description="atomic<int> 与 atomic<long long> 两个查询都是 1，16 字节的 atomic<Wide> 都是 0，它的动作落在内部锁上。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
atomic<int>        is_always_lock_free=1  is_lock_free=1
atomic<long long>  is_always_lock_free=1  is_lock_free=1
atomic<Wide>(16B)  is_always_lock_free=0  is_lock_free=0
```

`atomic<int>` 与 `atomic<long long>` 无锁，16 字节的 `Wide` 不是，两个查询都翻成 false，它的操作落在内部锁上。您写的代码要押注某段路径绝不能有锁，就把 `static_assert(std::atomic<Wide>::is_always_lock_free)` 压上去，平台不满足直接编不过，比运行时分支干净。换编译选项或者换平台，读数会变，所以拿不准就问它，别背答案。

## CAS：compare_exchange 的失败与重试

fetch 系列只覆盖加减与位运算，形状不合的读改写，比如只比最大值、只在没人动过时才换，得靠 **compare-and-swap**，简称 CAS，咱们把它请出来。`compare_exchange_weak` 与 `compare_exchange_strong` 的效果写在 [atomics.types.operations]/23，您把 expected 的值取出来，跟原子变量的值表示比一比，相等就写入 desired、返回 true，不相等就把原子变量当时的值写回 expected、返回 false。

CAS 失败的时候，有个容易漏的细节，expected 会被覆盖。笔者拿 `std::atomic<int>` 摆了两个场面。一个用 expected=99 去 CAS 值为 10 的变量，返回 false，expected 被覆盖成 10。另一个是加 5 的 CAS 循环，半路埋了个人改值，失败一轮、重算一轮、成功：

<OnlineCompilerDemo
  title="CAS 失败时 expected 被覆盖，重算重试"
  source-path="code/examples/vol5/chapter05/03_demo_cas.cpp"
  description="值对不上时 CAS 返回 false 并把当前值写回 expected。往 quota 加 5 的循环半路被人改值，失败一轮后拿新值重算成功。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_cas.cpp —— CAS 的两条路：expected 被覆盖，失败之后重算重试
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace std::chrono_literals;

std::atomic<int> quota{10};      // 咱们想往当前值上再叠加 5，前提是没人动过

void meddler() {                 // 半路把值改掉的人
  std::this_thread::sleep_for(50ms);
  quota.store(100);
}

int main() {
  // 第一步：值对不上，CAS 返回 false，顺带把当前值写回 expected
  std::atomic<int> v{10};
  int expected = 99;
  bool ok = v.compare_exchange_strong(expected, 20);
  std::printf("expected=99 去 CAS：ok=%d，v 还是 %d，expected 被覆盖成 %d\n",
              ok ? 1 : 0, v.load(), expected);

  // 第二步：读改写拆成 CAS 循环，中途值被改，失败一轮后重算成功
  std::thread m(meddler);
  expected = quota.load();                 // 读到 10
  std::this_thread::sleep_for(100ms);      // 故意把窗口拉开，让 meddler 赶在动手之前改值
  m.join();

  int attempts = 0;
  while (true) {
    int desired = expected + 5;
    if (quota.compare_exchange_strong(expected, desired)) {
      std::printf("第 %d 次尝试成功：%d + 5 -> %d\n", attempts + 1, expected, quota.load());
      break;
    }
    ++attempts;
    std::printf("第 %d 次尝试失败：值已经是 %d（expected 也被覆盖成它），拿它重算\n",
                attempts, expected);
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
expected=99 去 CAS：ok=0，v 还是 10，expected 被覆盖成 10
第 1 次尝试失败：值已经是 100（expected 也被覆盖成它），拿它重算
第 2 次尝试成功：100 + 5 -> 105
```

覆盖不是坏事，咱们把它当成 CAS 送回来的重算材料。循环重试正是把任意“读、算、写”压成一次读改写的通用办法，您读一份 expected，算出 desired，CAS 成功就完事，失败说明别人动了，expected 已经换成最新的，接着重算。第二步的加 5 走的就是它。

### weak 与 strong 差在哪

差别只有一项，weak 允许**虚假失败**。哪怕值相等，它也可以返回 false。标准在 [atomics.types.operations]/27 把原因写明，compare-and-exchange 在某些机器上靠 load-link/store-conditional 一对指令实现，SC 那半可能因为各种原因失败，weak 把这样的失败照实报出来，strong 则必须内部重试到成功或者值真的变了。cppreference 补的实际建议，咱们马上把它落成判断法。

落到判断法上，包在循环里的 CAS 用 weak，部分平台上它更快，虚假失败不过多转一圈。循环外一次性的判断用 strong，免得一次虚假失败把您带错分支。常见的 x86 走 CMPXCHG 指令，没有虚假失败一说，weak 与 strong 在上面跑起来几乎无差别，可代码是写给所有平台的，照判断法写不吃亏。

### CAS 只认值，不认历史：一次可复现的 ABA

CAS 比较的是值表示，值相等就放行。它不问的问题是，这个值在中间变过没有。变过去又变回来，CAS 照样成功，这就是 **ABA**。空口说没有分量，笔者把它编排成确定复现的场面，一个用槽位数组搭的栈，head 存槽位号，出栈的人读完 head、睡 150 毫秒再 CAS。睡的空当里 main 把两个节点都弹走，再把新值塞进腾出来的槽 1，head 指回 1。另一版把版本号打进 head，咱们看看下场差在哪：

<OnlineCompilerDemo
  title="CAS 只认值不认历史：可复现的 ABA 与版本号修法"
  source-path="code/examples/vol5/chapter05/04_demo_aba.cpp"
  description="前半场 head 被弄回旧槽位，出栈线程的 CAS 照样放行，300 不见。后半场版本号打进 head，CAS 当场拦下。演示用，槽位数组只靠时间窗编排，存在数据竞争。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_aba.cpp —— CAS 只认值，不认历史：一次可复现的 ABA，和版本号的修法
// 演示用：槽位数组 slots 是普通数组，只靠时间窗编排读写次序，存在数据竞争
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace std::chrono_literals;

constexpr int kEmpty = -1;

struct Node { int value; int next; };

// 版本一：head 只存槽位号，CAS 只问“号码对不对”
struct NaiveStack {
  std::atomic<int> head{kEmpty};
  Node slots[3] = {};

  void init() {
    slots[1] = {100, 2};              // 槽 1：值 100，下一个是槽 2
    slots[2] = {200, kEmpty};
    head.store(1);
  }
  int pop_with_window() {             // 出栈：读完 head 故意留一个窗口再 CAS
    int cur = head.load();
    if (cur == kEmpty) return kEmpty;
    int nxt = slots[cur].next;
    std::this_thread::sleep_for(150ms);
    if (!head.compare_exchange_strong(cur, nxt)) return kEmpty;
    return slots[cur].value;
  }
  void push(int idx, int value) {
    slots[idx] = {value, head.load()};
    head.store(idx);
  }
};

// 版本二：head 把版本号和槽位号打包，动一次版本涨一次
struct VersionStack {
  static unsigned long long pack(int idx, unsigned long long ver) {
    return (ver << 8) | (unsigned long long)(unsigned char)idx;
  }
  std::atomic<unsigned long long> head{pack(kEmpty, 0)};
  Node slots[3] = {};

  void init() {
    slots[1] = {100, 2};
    slots[2] = {200, kEmpty};
    head.store(pack(1, 0));
  }
  int pop_with_window() {
    unsigned long long cur = head.load();
    int idx = (int)(cur & 0xFF);
    if (idx == kEmpty) return kEmpty;
    int nxt = slots[idx].next;
    std::this_thread::sleep_for(150ms);
    for (int attempt = 1; ; ++attempt) {
      unsigned long long want = pack(nxt, (cur >> 8) + 1);
      unsigned long long stale = cur;
      if (head.compare_exchange_strong(cur, want))
        return slots[idx].value;
      cur = head.load();
      std::printf("    A 第 %d 次 CAS 失败：手里的 head=%llu，现在已经是 %llu\n",
                  attempt, stale, cur);
      idx = (int)(cur & 0xFF);
      if (idx == kEmpty) return kEmpty;
      nxt = slots[idx].next;
    }
  }
  void push(int idx, int value) {
    unsigned long long cur = head.load();
    slots[idx] = {value, (int)(cur & 0xFF)};
    head.store(pack(idx, (cur >> 8) + 1));
  }
};

std::atomic<bool> a_read{false};

void wait_for_a() {                   // 手动轮询等 A 读完（demo 有意不用 wait/notify）
  while (!a_read.load()) {}
}

void run_naive() {
  NaiveStack s;
  s.init();
  std::thread a([&] {
    int cur = s.head.load();
    int nxt = s.slots[cur].next;
    std::printf("[naive  ] A 读完：head=%d，下一个是槽 %d，随后睡 150 ms\n", cur, nxt);
    a_read.store(true);
    int got = s.pop_with_window();
    std::printf("[naive  ] A 的 CAS(head: 1 -> 2) 成功，它报告 pop 到了 %d\n", got);
  });
  wait_for_a();
  std::this_thread::sleep_for(10ms);
  int first = s.slots[1].value;
  s.head.store(s.slots[1].next);      // main 的第一次 pop：head 1 -> 2
  std::printf("[naive  ] main: pop 收到 %d\n", first);
  int second = s.slots[2].value;
  s.head.store(s.slots[2].next);      // 第二次 pop：head 2 -> 空
  std::printf("[naive  ] main: pop 收到 %d\n", second);
  s.push(1, 300);                     // 复用槽 1，head 指回 1
  std::printf("[naive  ] main: 把 300 放进槽 1，head 指回 1\n");
  a.join();
  int top = s.head.load();
  std::printf("[naive  ] main 清点：head=%d，栈里只剩值 %d。300 不见了，200 已经交过一回货\n",
              top, s.slots[top].value);
}

void run_version() {
  VersionStack s;
  s.init();
  std::thread a([&] {
    unsigned long long cur = s.head.load();
    std::printf("[version] A 读完：head=%llu（版本 %llu，槽 %llu），随后睡 150 ms\n",
                cur, cur >> 8, cur & 0xFF);
    a_read.store(true);
    int got = s.pop_with_window();
    std::printf("[version] A 最终 pop 到 %d\n", got);
  });
  wait_for_a();
  std::this_thread::sleep_for(10ms);
  s.head.store(VersionStack::pack(s.slots[1].next, 1));
  std::printf("[version] main: pop 收到 %d\n", s.slots[1].value);
  s.head.store(VersionStack::pack(s.slots[2].next, 2));
  std::printf("[version] main: pop 收到 %d\n", s.slots[2].value);
  s.push(1, 300);
  std::printf("[version] main: 把 300 放进槽 1，head=%llu（版本 3，槽 1）\n", s.head.load());
  a.join();
  std::printf("[version] main 清点：head=%llu，槽位是空标记，栈空了。300 交给 A，数目一致\n",
              s.head.load());
}

int main() {
  run_naive();
  a_read.store(false);
  run_version();
  return 0;
}
```

</OnlineCompilerDemo>

```text
[naive  ] A 读完：head=1，下一个是槽 2，随后睡 150 ms
[naive  ] main: pop 收到 100
[naive  ] main: pop 收到 200
[naive  ] main: 把 300 放进槽 1，head 指回 1
[naive  ] A 的 CAS(head: 1 -> 2) 成功，它报告 pop 到了 300
[naive  ] main 清点：head=2，栈里只剩值 200。300 不见了，200 已经交过一回货
[version] A 读完：head=1（版本 0，槽 1），随后睡 150 ms
[version] main: pop 收到 100
[version] main: pop 收到 200
[version] main: 把 300 放进槽 1，head=769（版本 3，槽 1）
    A 第 1 次 CAS 失败：手里的 head=1，现在已经是 769
[version] A 最终 pop 到 300
[version] main 清点：head=1279，槽位是空标记，栈空了。300 交给 A，数目一致
```

naive 的六行是事故现场，咱们一起对一遍。A 手里的旧值是 1，CAS 只问“现在还是 1 吗”，main 恰好把 head 弄回了 1，CAS 放行，A 把 head 改成 2。后果跟着出来，A 报告 pop 到 300，可 main 明明刚把 300 压进栈。清点时栈里只剩 200，200 是 main 已经弹走交过货的节点。值对上了，事情全错了。

version 是修法，咱们给 head 升级，从 int 涨到 64 位，低 8 位装槽位号，高位装版本号，每次进出栈版本涨一。A 手里的 head 是 1（版本 0），main 三次进出之后 head 是 769（版本 3，槽 1），CAS 当场不放行，手里的 1 对不上现在的 769。A 重读、重算，把 300 正确地弹走，栈面干净。真正的无锁栈里这个套路叫**带标签的指针**，思路一样，把历史塞进比较的字节里。完整的无锁栈还要解决内存回收，归 cookbook 选读的无锁入门，还没成文。

## 参考资源

实验的现场记录交代一遍。ABA demo 的场面是编排出来的，线程次序写死，读数可复现。demo_ops 的交回值、demo_lockfree 的查询结果每次运行都一致，demo_cas 的成功行落在第 2 次尝试，由 50 毫秒与 100 毫秒两个时长定死。正文方括号里的是草案小节名，小节名跟正式标准一致，段号可能挪。cppreference 的 atomic 与 compare_exchange 两页都抓到了全文，与草案对读无出入。推进顺序参照了 Anthony Williams 的《C++ Concurrency in Action》，例子与文字都是笔者自己写的。

- [std::atomic（cppreference）][cppref-atomic]，特化分档、操作面与 C++17 的 is_always_lock_free。
- [compare_exchange_weak 与 strong（cppreference）][cppref-cas]，虚假失败、expected 的覆盖、按值表示比较的版本变化。
- [Class template atomic（现行草案 atomics.types.generic）][draft-generic]，可平凡复制等编译期要求，整型、浮点、指针与智能指针特化的分工，bool 特化走主模板的注。
- [Operations on atomic types（现行草案 atomics.types.operations）][draft-ops]，CAS 的效果与虚假失败注、is_lock_free 的说明、wait 与 notify。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，推进顺序参考。

[cppref-atomic]: https://en.cppreference.com/w/cpp/atomic/atomic
[cppref-cas]: https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange
[draft-generic]: https://eel.is/c++draft/atomics.types.generic
[draft-ops]: https://eel.is/c++draft/atomics.types.operations
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
