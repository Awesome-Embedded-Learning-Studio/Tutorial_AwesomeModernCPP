---
title: "How Far Atomic Operations Take You: Types, Operations, and CAS"
description: "Which types std::atomic accepts and which operations it grants: the operation surface of load/store/fetch_add, the inquiry points for lock-free versus internal locks, CAS failure and retry with the division of labor between weak and strong, and an ABA scene with the version-number fix."
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
translation:
  source: documents/vol5-concurrency/chapter05-atomic-memory-model/01-what-atomics-solve.md
  source_hash: b8e8e6e96305911400ecf1889d03d78d9db79972ffa8c60fb9280067bb6db07b
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 13339
---

# How Far Atomic Operations Take You: Types, Operations, and CAS

The job I set for myself here is a single one: draw the boundary between what atomic operations guarantee and what they don't.

Most demos in this article are staged, deterministic scenes — who reads, who sleeps, who acts, with the order fixed in code, so re-running gives the same readings.

## Which Types std::atomic Accepts, Which Operations It Grants

`std::atomic` is a class template living in `<atomic>`, since C++11. Meet it as a set of types plus a set of operations: the type decides which operations you get, and the operation decides the shape of one action. Viewing by tier is the easiest way.

| Type tier | Granted operations | Version |
| --------- | ----------------------------------------------------------------------------------------------------------- | --------------------------------- |
| Integers: every character type and every standard integer type, including the `<cstdint>` typedefs | load, store, exchange, CAS; fetch_add, fetch_sub, fetch_and, fetch_or, fetch_xor; `++`, `--`, `+=` and other operators | since C++11 |
| Floating point: `float`, `double`, `long double` | load, store, exchange, CAS; fetch_add, fetch_sub | since C++20 |
| Pointers `atomic<T*>` | load, store, exchange, CAS; fetch_add, fetch_sub (offset by element count) | since C++11 |
| Trivially copyable custom types (the primary template) | load, store, exchange, CAS; wait, notify | since C++11; wait and notify belong to C++20 |
| `atomic<std::shared_ptr<T>>`, `atomic<std::weak_ptr<T>>` | load, store, exchange, CAS; wait, notify | since C++20 |

Two spots easy to misread, stated plainly. `atomic<bool>` goes through the primary template — the standard's note says explicitly that the integer specializations don't govern it (note 1 at [atomics.types.int]) — so bool carries no addition and `fetch_add` doesn't exist. The standard is still growing: C++26 adds `fetch_max` and `fetch_min` for integers and pointers; this volume stops at C++20, and knowing they exist is enough.

On the operation surface, the `fetch_` family does read, compute, and write as one action, handing back the old value from before the operation. Let's run a minimal stretch:

<OnlineCompilerDemo
  title="What load, exchange, fetch_add, and the Operators Each Hand Back"
  source-path="code/examples/vol5/chapter05/01_demo_ops.cpp"
  description="On one atomic<int>, run store, exchange, fetch_add, prefix/postfix ++, and += in turn, printing the handed-back value and the current value at each step."
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

Handing back the old value is deliberate: one action finishes both reading the current state and changing it, which is handy for counters and quotas. The operator forms have their own temperament — prefix `++` hands back the new value, postfix the old, matching ordinary `int` rules. `+=` is fetch_add dressed in an operator coat. When the return value feeds other computations, write the explicit `fetch_add`; the intent reads clearer.

## Atomic Doesn't Mean Lock-Free: is_lock_free and is_always_lock_free

Up to here I've kept saying "one action" without accounting for who provides it. In most people's heads, atomic equals one CPU instruction done; the fact is, when the type is small enough and the platform has the instruction, the compiler gives you the real instruction. When the type is too wide for any matching instruction (16 bytes, say), the library falls back to fencing the action with an internal lock — the action stays whole, at the price of a lock genuinely inside. What `std::atomic` guarantees is the whole action, not the absence of a lock.

The standard library provides two inquiry points. `is_always_lock_free` is a compile-time constant (since C++17); true means that type's atomic operations are lock-free forever on this platform. `is_lock_free()` is a runtime query. On the local x86-64:

<OnlineCompilerDemo
  title="is_lock_free and is_always_lock_free: Wide Types Are Not Lock-Free"
  source-path="code/examples/vol5/chapter05/02_demo_lockfree.cpp"
  description="atomic<int> and atomic<long long> read 1 on both queries; the 16-byte atomic<Wide> reads 0 on both, its actions landing on the internal lock."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
atomic<int>        is_always_lock_free=1  is_lock_free=1
atomic<long long>  is_always_lock_free=1  is_lock_free=1
atomic<Wide>(16B)  is_always_lock_free=0  is_lock_free=0
```

`atomic<int>` and `atomic<long long>` are lock-free; the 16-byte `Wide` is not — both queries flip to false, and its operations land on the internal lock. If your code bets that some path must never take a lock, press `static_assert(std::atomic<Wide>::is_always_lock_free)` on it: a platform that fails simply doesn't compile, cleaner than a runtime branch. Change compiler options or platforms and the readings change — so when unsure, ask it; don't memorize answers.

## CAS: compare_exchange's Failure and Retry

The fetch family covers only arithmetic and bitwise shapes; a read-modify-write that doesn't fit — keep only the maximum, swap only if untouched — needs **compare-and-swap**, CAS for short, so let's bring it out. The effects of `compare_exchange_weak` and `compare_exchange_strong` are written at [atomics.types.operations]/23: take the expected value, compare it against the atomic variable's value representation; equal, write desired and return true; unequal, write the atomic's current value back into expected and return false.

On CAS failure there is an easily missed detail: expected gets overwritten. I staged two scenes on `std::atomic<int>`. One CASes a variable holding 10 with expected=99 — it returns false and expected is overwritten to 10. The other is an add-5 CAS loop with a meddler changing the value mid-way: one failed round, one recompute, success:

<OnlineCompilerDemo
  title="On CAS Failure, expected Is Overwritten — Recompute and Retry"
  source-path="code/examples/vol5/chapter05/03_demo_cas.cpp"
  description="When values mismatch, CAS returns false and writes the current value back into expected. In the add-5-to-quota loop, a meddler changes the value mid-way; after one failed round, the loop recomputes with the new value and succeeds."
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

The overwriting is no bad thing — treat it as the recomputation material CAS sends back. Loop-retry is precisely the general method for compressing any "read, compute, write" into one read-modify-write: read an expected, compute a desired; CAS succeeds and you're done, fails and it means someone acted — expected already holds the latest, so compute again. The add-5 in step two walks exactly this road.

### Where weak and strong Differ

The difference is a single item: weak allows **spurious failure**. Even with equal values, it may return false. The standard states the reason at [atomics.types.operations]/27 — on some machines, compare-and-exchange is built on the load-link/store-conditional instruction pair, and the SC half can fail for various reasons; weak reports such failures honestly, while strong must retry internally until success or a genuine value change. cppreference adds practical advice, which we now turn into a rule of thumb.

As a rule of thumb: for a CAS inside a loop, use weak — on some platforms it is faster, and a spurious failure merely adds one more lap. For a one-shot decision outside a loop, use strong, so a spurious failure can't take you down the wrong branch. The common x86 runs the CMPXCHG instruction, where spurious failure doesn't exist and weak versus strong makes almost no difference — but code is written for all platforms, and following the rule costs nothing.

### CAS Recognizes Values, Not History: A Reproducible ABA

CAS compares value representations and lets equal values pass. The question it never asks is whether the value changed in between. Changed away and changed back, and CAS still succeeds — that is **ABA**. Words alone carry no weight, so I staged a deterministically reproducible scene: a stack built on a slot array, head storing a slot number; the popper reads head, sleeps 150 ms, then CASes. During the sleep, main pops both nodes, pushes a new value into the freed slot 1, and head points back to 1. Another version packs a version number into head — watch how the outcomes differ:

<OnlineCompilerDemo
  title="CAS Recognizes Values, Not History: A Reproducible ABA and the Version-Number Fix"
  source-path="code/examples/vol5/chapter05/04_demo_aba.cpp"
  description="In the first half, head is maneuvered back to the old slot and the popping thread's CAS still waves it through — 300 vanishes. In the second half, a version number is packed into head and CAS stops it on the spot. For demonstration only: the slot array's read/write order is orchestrated purely by timing windows, so a data race exists."
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

Naive's six lines are the accident scene; let's check them together. The old value in A's hand is 1; CAS only asks "is it still 1 now?"; main happens to have steered head back to 1, so CAS waves it through and A sets head to 2. The consequences follow: A reports popping 300, yet main had just pushed 300 onto the stack. At the count, only 200 remains in the stack — 200 being the node main already popped and delivered. The values matched, and everything went wrong.

version is the fix: we upgrade head from int to 64 bits — the low 8 bits hold the slot number, the high bits the version, bumped on every push and pop. The head in A's hand is 1 (version 0); after main's three ins and outs, head is 769 (version 3, slot 1), and CAS refuses on the spot — the 1 in hand doesn't match the 769 now. A re-reads, recomputes, and correctly pops the 300, leaving the stack face clean. In real lock-free stacks this trick is called **tagged pointers**; the idea is the same — stuff history into the compared bytes. A complete lock-free stack must also solve memory reclamation, which belongs to the cookbook's lock-free introduction elective, not yet written.

## Reference Resources

The experiments' scene records, stated once. The ABA demo's scene is staged, with thread order fixed in code and reproducible readings. demo_ops' handed-back values and demo_lockfree's query results are identical on every run, and demo_cas's success line lands on attempt 2, pinned by the two durations of 50 ms and 100 ms. The brackets in the text hold draft clause names; clause names match the official standard, and paragraph numbers may drift. cppreference's atomic and compare_exchange pages were both fetched in full and read against the draft with no discrepancies. The progression follows Anthony Williams's *C++ Concurrency in Action*; the examples and prose are my own.

- [std::atomic (cppreference)][cppref-atomic], the specialization tiers, the operation surface, and C++17's is_always_lock_free.
- [compare_exchange_weak and strong (cppreference)][cppref-cas], spurious failure, the overwriting of expected, and the change history of comparing by value representation.
- [Class template atomic (current draft, atomics.types.generic)][draft-generic], the compile-time requirements like trivial copyability, the division of labor among the integer, floating-point, pointer, and smart-pointer specializations, and the note that the bool specialization goes through the primary template.
- [Operations on atomic types (current draft, atomics.types.operations)][draft-ops], CAS's effects and the spurious-failure note, the description of is_lock_free, and wait and notify.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams], the reference for the progression.

[cppref-atomic]: https://en.cppreference.com/w/cpp/atomic/atomic
[cppref-cas]: https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange
[draft-generic]: https://eel.is/c++draft/atomics.types.generic
[draft-ops]: https://eel.is/c++draft/atomics.types.operations
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
