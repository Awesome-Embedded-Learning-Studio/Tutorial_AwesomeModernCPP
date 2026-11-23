---
title: "atomic_flag and the Boundary of Atomicity: One Action Is Not One Transaction"
description: "atomic_flag, the standard's one lock-free guarantee: a hand-rolled spinlock and the history of ATOMIC_FLAG_INIT; how to draw the line between one atomic action and one transaction; the common misconceptions judged one by one; and where floating-point and custom types each get stuck."
chapter: 5
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - atomic
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter05-atomic-memory-model/02-atomic-flag-and-boundary.md
  source_hash: 77edf270d3b48195a254ed69e29bd44b8eea708d973e015cf8f38994cf391635
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 14581
---

# atomic_flag and the Boundary of Atomicity: One Action Is Not One Transaction

What can and cannot be guaranteed was drawn up to the CAS stop in the previous article. This one looks at atomic_flag, the exception the standard guarantees lock-free, then stands up the boundary between "one action" and "one transaction", and counts the common misconceptions.

## atomic_flag: The Standard's Lock-Free Exception

Whether `std::atomic<T>`'s operations are lock-free depends on the platform; `std::atomic_flag` is the exception — cppreference's words are it is guaranteed to be lock-free, and the standard guarantees it. The price is visible too: an extremely narrow operation surface — `test_and_set` sets the flag true and hands back the old, `clear` sets it false. Before C++20 there wasn't even a glance-read; `test()`, `wait()`, and `notify_*` were all added in C++20.

Its classic use is a hand-rolled spinlock, whose shape we already met in the starvation demo of [*Same Counter, Two Threads — Why Does It Compute Wrong?*](../chapter02-sharing-data/01-from-wrong-programs) (that side used the `ATOMIC_FLAG_INIT` spelling). Now let's run it on its own, two threads each incrementing 100000 times:

<OnlineCompilerDemo
  title="An atomic_flag Spinlock: test_and_set Takes, clear Releases"
  source-path="code/examples/vol5/chapter05/05_demo_flag.cpp"
  description="After default construction test() reads 0; two threads each add 100000 times, and guarded lands exactly on 200000."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_flag.cpp —— atomic_flag 自旋锁：标准唯一保证无锁的原子类型
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic_flag busy{};      // C++20 起默认构造就是清除态；C++11 得写 ATOMIC_FLAG_INIT

void lock_flag() { while (busy.test_and_set(std::memory_order_acquire)) {} }
void unlock_flag() { busy.clear(std::memory_order_release); }

long long guarded = 0;        // 普通变量，保护全靠上面那把自旋锁

void work(int rounds) {
  for (int i = 0; i < rounds; ++i) {
    lock_flag();
    ++guarded;
    unlock_flag();
  }
}

int main() {
  std::printf("flag 默认构造后 test(): %d\n", busy.test() ? 1 : 0);
  std::thread a(work, 100000);
  std::thread b(work, 100000);
  a.join();
  b.join();
  std::printf("两条线程各加 100000 次，结果 %lld\n", guarded);
  return 0;
}
```

</OnlineCompilerDemo>

```text
flag 默认构造后 test(): 0
两条线程各加 100000 次，结果 200000
```

200000 exactly. The lock's usage and its starvation flaw were both measured in 2.1, *Same Counter, Two Threads — Why Does It Compute Wrong?*; what we add here is the initialization history. In the C++11 era, an `atomic_flag` had exactly one road to a definite initial value: `std::atomic_flag f = ATOMIC_FLAG_INIT;` — any other spelling left the value unspecified. Since C++20, default construction gives the clear state, which is where the demo's `test()` printing 0 comes from; the macro is no longer necessary, and 2.1's old spelling still works.

The acquire and release inside the parentheses of `test_and_set` and `clear` are memory-order vocabulary; 2.1's samples carried them too, and writing them as-is suffices here — the vocabulary gets its dedicated treatment in 5.4, *The Four Tiers of Memory Order: From Sequential Consistency to relaxed*.

> A side note for the record: C++20 briefly deprecated the macro, and defect report LWG 3659 un-deprecated it, on the grounds that C on some platforms still needs it. When moving code between C and C++, don't take its appearance for an error.

On the spinlock side, a thread that can't get the lock spins in place — worthwhile only when the critical section is nanoseconds short; any longer burns CPU, and we go back to `std::mutex`. C++20's `wait` and `notify_*` can turn the spinning into suspension; the cookbook's atomic_wait recipe isn't written yet, and we'll pick it up then.

## One Action and One Transaction: Where to Draw the Line

The previous article's operations were all single actions: fetch_add is one read-modify-write, a successful CAS is one read-modify-write, a failed one a read. The scene left by [*Same Counter, Two Threads — Why Does It Compute Wrong?*](../chapter02-sharing-data/01-from-wrong-programs) is still uncleaned: the `atomic<int>` counter ran 8 times without once reaching 200000, because `counter = counter.load() + 1` is three steps. Atomicity guarantees each step whole, not the three steps fused into one. The closing line of 2.4, *Where Is the Boundary of One Operation*, on check-then-act is exactly this section's problem statement: atomics govern single actions; compound operations need the actions joined. There are two ways to join them, and we'll run each.

Let's set the scene back on 2.4's shelf: one item left, two buyers. The stock is now a `std::atomic<int>` — every step atomic, the check one load, the deduction a load followed by a store. In an orchestrated window, both buyers see 1 and both act:

<OnlineCompilerDemo
  title="Check and Act as Two Atomic Operations — the Item Still Oversells"
  source-path="code/examples/vol5/chapter05/06_demo_boundary.cpp"
  description="With stock as atomic<int>, both buyers see 1 and both act, leaving -1. A CAS loop presses the check and the deduction into one read-modify-write, and one item goes to exactly one buyer."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
[naive  ] buyer B 动手，结余 0
[naive  ] buyer A 动手，结余 -1
[cas    ] buyer B 动手，结余 0
[cas    ] buyer A 重试看见 0，放弃
```

Balance -1: one item sold to two people, dying the same death as 2.3's shelf that took the lock but split its critical section. Replacing every step with an atomic operation doesn't cure a wrongly drawn boundary — the window between checking and acting is beyond atomicity's reach.

Fix A hides in the demo's second half: press the business action into one read-modify-write. Check-plus-deduct can't be written as one fetch_add — arithmetic has no conditions — but a CAS loop holds it: read the old value; if zero, walk away; if not, CAS it to old-minus-one; on failure, re-read and retry. buyer B took the one item; buyer A's CAS failed, the re-read saw 0, and it gave up — balance 0, one item one person.

Fix B is for when it can't be pressed: the lock returns. Which can't be pressed? Let's count. Invariants spanning two objects — 2.4's wallet, where A's decrement and B's increment must hold together. Compound conditions — the queue of [*A Queue with Capacity: Getting It to Stand First*](../chapter03-wait-and-coordinate/03-bounded-queue), with empty, full, and closed three conditions meshed. Plus scenes that must call other code before and after acting, or roll back elsewhere on failure. Atomics and locks divide the labor differently: what presses into one action goes to atomics; what doesn't goes to a lock or a redesign.

The peak gauge from 3.7, *Choosing Tools by What the Coordination Needs*, we can now read fluently:

<OnlineCompilerDemo
  title="The Peak Gauge: A Ratchet CAS Loop"
  source-path="code/examples/vol5/chapter05/07_peak_gauge.cpp"
  description="The in-text three lines live verbatim inside task: inside counts currently-online tasks, and whoever measures a larger now ratchets peak up. At the end inside returns to 0 with the peak at 4."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// peak_gauge.cpp —— 峰值仪表：只许涨不许跌的 CAS 循环（3.7 量峰值片段的补全版）
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

std::atomic<int> inside{0};    // 当前同时在线的任务数
std::atomic<int> peak{0};      // 峰值仪表：只许涨，不许跌

void task() {
  int now = ++inside;
  int old = peak.load();
  while (now > old && !peak.compare_exchange_weak(old, now)) {}
  std::this_thread::sleep_for(10ms);   // 在里面待一会儿，让并发叠起来
  --inside;
}

int main() {
  std::vector<std::thread> ts;
  for (int i = 0; i < 4; ++i) ts.emplace_back(task);
  for (auto& t : ts) t.join();
  std::printf("4 个任务做完，inside=%d，峰值 %d\n", inside.load(), peak.load());
  return 0;
}
```

</OnlineCompilerDemo>

peak only ratchets up: whoever measures a larger now CASes it up; the loser receives the latest peak and competes again next round. Count its actions — one read-modify-write plus a loop keeps it whole, without even inviting a lock.

## The Common Misconceptions

The old ch03 directory's source article, *atomic operations*, collected a misconceptions section; we rewrote it into the judgment version below.

### volatile Is Not atomic

`volatile` blocks compiler optimization, guaranteeing every read and write truly lands in memory — it guarantees neither whole actions nor ordering. `++volatile_counter` is still read, add, write; two threads at once still lose counts. And judged by 2.1's four questions, conflicting accesses on a volatile variable get no atomic exemption — it is a data race, and the standard rules undefined behavior. volatile's day job is hardware register mapping and signal handling, unrelated to multithreading; the part-time atomic role is your own imagination.

### std::string Cannot Be Built into an atomic

The compile probe is one line: construct a `std::atomic<std::string>` object, and GCC 16 replies:

```text
In file included from probe_str.cpp:1:
/usr/include/c++/16/atomic: In instantiation of ‘struct std::atomic<std::__cxx11::basic_string<char> >’:
probe_str.cpp:3:26:   required from here
    3 | std::atomic<std::string> s;
      |                          ^
/usr/include/c++/16/atomic:215:21: error: static assertion failed: std::atomic requires a trivially copyable type
```

What blocked it, stated clearly: **trivially copyable** — copying the type amounts to moving the bytes as-is, while `std::string` manages heap memory, and moving house is not moving bytes. The C++11 text wrote it as a requirement with no diagnostic on violation; since C++20 it is promoted to a compile-time check — fail it and compilation fails. Trivially copyable custom types compile, with an operation surface of only load, store, exchange, and CAS; the extension section details it.

> The comparison method, stated clearly: CAS compares by bytes — [atomics.types.operations] note 4's original words say it behaves as memcmp plus memcpy. It calls neither your operator== nor your copy constructor. For a custom type used atomically, equal means byte-equal.

### What Default Construction Yields

One glance, measured:

<OnlineCompilerDemo
  title="Since C++20, Default Construction Yields a Definite Zero"
  source-path="code/examples/vol5/chapter05/08_demo_init.cpp"
  description="atomic<int> default-constructed then loaded reads 0, atomic_flag default-constructed then test()ed reads 0 — definite starting points that exist only since C++20."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
atomic<int> 默认构造 load: 0
atomic_flag 默认构造 test: 0
```

Both are definite starting points — C++20 behavior. The history is easy to read backwards: in the C++11 text, default construction didn't even construct a T object, and what you read was undefined. LWG 2334 amended the clause to "value-initialize with T()", and libstdc++ and libc++ only followed in C++20 mode — cppreference records this on the constructor page. So older code habitually constructs with an initial value, `std::atomic<int> c{0}`; keep the habit at no loss — under the new standard both spellings get a definite starting point.

### Every Operation Atomic, Two Variables Together Still Unordered

The source article's example deserves another look: thread one does `x.store(1)` then `y.store(2)`; thread two reads y then x. Each variable's reads and writes are whole, yet thread two may read y as 2 and x as 0. Under default seq_cst this scene is unreachable — it requires all seq_cst operations to line up in one total order, with x's write before y's read; only relaxing the reads to relaxed makes it possible. Why relax and how to judge belong to 5.3, *How One Thread Observes Another's Modifications*, and 5.4, *The Four Tiers of Memory Order: From Sequential Consistency to relaxed*. Here I only stand the boundary up: atomic wholeness is spoken per variable; which of two variables comes first, this article's tools cannot answer.

## Atomic Boundaries for Floating Point and Custom Types (Extension)

This section carries the (extension) mark; skipping it does not affect the unit's main line. For friends putting atomics on floating-point counters or custom structures, the boundaries sit in the two subsections below.

### Floating Point: Atomic Add and Subtract Since C++20, One Rounding per Step

C++20 granted `atomic<float>`, `atomic<double>`, and `atomic<long double>` fetch_add and fetch_sub — no multiply or divide. The semantic key sits in rounding: every fetch_add computes the floating-point addition once more and rounds once more. The demo starts from 2 to the 24th power minus 1:

<OnlineCompilerDemo
  title="Atomic Floating-Point Addition: Every Step Whole, the Number Goes Nowhere"
  source-path="code/examples/vol5/chapter05/09_demo_float.cpp"
  description="Adding 1 to 16777215 moves once; a hundred more atomic additions stay put. In the control segment with four threads adding a million each, float sticks while long long counts fully."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_float.cpp —— 浮点原子加法：每一步都完整，合起来数字没动
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

int main() {
  std::atomic<float> f{16777215.0f};          // 2 的 24 次方减 1
  float r1 = f.fetch_add(1.0f);
  std::printf("16777215 + 1 -> %.0f（交回旧值 %.0f），这一步动了\n", f.load(), r1);
  for (int i = 0; i < 100; ++i) f.fetch_add(1.0f);
  std::printf("再原子加 100 次 1.0 -> %.0f，卡住了\n", f.load());

  std::atomic<float> acc{16777216.0f};        // 四条线程各加 100 万次 1.0
  std::atomic<long long> ref{16777216LL};
  std::vector<std::thread> ts;
  for (int t = 0; t < 4; ++t) {
    ts.emplace_back([&] {
      for (int i = 0; i < 1000000; ++i) {
        acc.fetch_add(1.0f);
        ref.fetch_add(1);
      }
    });
  }
  for (auto& t : ts) t.join();
  std::printf("4 条线程各加 100 万次后：float=%.0f   long long=%lld\n", acc.load(), ref.load());
  return 0;
}
```

</OnlineCompilerDemo>

```text
16777215 + 1 -> 16777216（交回旧值 16777215），这一步动了
再原子加 100 次 1.0 -> 16777216，卡住了
4 条线程各加 100 万次后：float=16777216   long long=20777216
```

The first addition truly moved — 16777216 is representable, and the result landed on it. Every later addition stayed in place: float's mantissa holds only 24 bits, 16777217 doesn't fit, and the round-to-nearest falls back to 16777216 — every step whole, every step wasted. The control segment with four threads adding a million each is blunter still: float never moves, long long counts to 20777216. cppreference added two C++20 notes on the fetch_add page, recorded here: when the result exceeds the representable range, the value is unspecified but there is no undefined behavior; and the floating-point environment may differ from the calling thread's. To count in floating point, swap the smallest unit for an integer (count cents, count nanoseconds), atomically add integers, and convert on reading — the most worry-free, unglamorous method.

One more byte-comparison corner, taken on its own. CAS compares by value representation: on implementations conforming to ISO/IEC 60559, -0.0 and +0.0 are `==`-equal but byte-unequal; NaNs with the same payload are byte-equal but `==`-unequal ([atomics.types.operations] note 6). If you judge floating-point equality through CAS, your judgment must follow the bytes.

### Custom Types: Past Trivial Copyability, Only Four Operations

Let's put `Tick` under the knife; it looks like this:

<OnlineCompilerDemo
  title="A Custom Type Past Trivial Copyability Is Left with Four Operations"
  source-path="code/examples/vol5/chapter05/10_atomic_tick.cpp"
  description="Tick passes the trivially-copyable check; atomic<Tick> constructs, and load, store, exchange, and CAS each run. fetch_add doesn't compile — the probe's reply is below."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

Two fields, trivially copyable, static_assert passes, `std::atomic<Tick>` constructs — and the operation surface is exactly the four counted before: load, store, exchange, CAS. Want a round of fetch_add on it, say to advance seq? Feed the probe to the compiler; the reply is below:

```text
probe_fetch.cpp: In function ‘int main()’:
probe_fetch.cpp:12:16: error: ‘struct std::atomic<Tick>’ has no member named ‘fetch_add’
   12 | int main() { t.fetch_add(1); return 0; }
      |                ^~~~~~~~~
```

No fetch_add — the arithmetic surface goes only to the integer specializations. A CAS loop still presses any read-modify-write into one action, and the whole toolkit from the previous article's CAS section applies. Two byte-level cautions, given by the standard's notes: padding bits that never participate in the value representation are ignored (since C++20), so structures with padding are less prone to spurious failure; and in a union, some bits participate in only some members' value representations, so CAS may fail forever ([atomics.types.operations] notes 7 and 8).

Two ready-made exits, stated together. `atomic<std::shared_ptr<T>>` is a C++20 partial specialization: types the primary template won't admit, the library knows personally — swap a shared pointer atomically without managing reference counts yourself, and it's the road. `std::atomic_ref` (C++20) slips atomic operations over an ordinary object temporarily; the cookbook's atomic_ref recipe isn't written yet, and we'll pick it up then.

## The Embedded View: ISRs, LDREX, and 64-bit Atomics (Extension)

This section carries the (extension) mark; skipping it does not affect this article's main line. The content faces readers writing concurrency on MCUs; the drafts of the three parts sit in the old directory's *atomic operations*, reassembled here in order from instructions to locks to interrupts.

### LDREX and STREX: How Atomics Land on M-profile Cores

ARMv7-M cores like the M3, M4, and M7 carry a pair of exclusive-access instructions: the exclusive read `LDREX` and the exclusive write `STREX`. `LDREX` reads the value in and, in passing, plants an exclusive tag on the address; `STREX` writes the new value back — if the tag still stands, the write succeeds and the instruction returns 0. If anyone interfered, most typically an interrupt arriving, the write is voided on the spot and it returns 1, and the loop the library wraps around them uses that return to decide whether to retry or finish. The scholarly name for the pair is load-link/store-conditional; 5.1, *How Far Atomic Operations Take You: Types, Operations, and CAS*, mentioned them on CAS spurious failure — the SC half can fail for no reason, weak reports the failure honestly, strong adds the retry internally — and these are their true bodies on M-profile cores.

That is exactly how the standard library's atomic operations land on these cores. One call to `fetch_add` is expanded by the compiler and runtime into a round of reading the old value, computing the new value in a register, and writing back with `STREX`; if the write doesn't stick, it backs up and retries — CAS's compare-and-swap and `atomic_flag`'s `test_and_set` likewise press down on this instruction pair. ARMv6-M cores like the M0 and M0+ lack even this pair; there the toolchain must find another road for read-modify-write, commonly a brief interrupt-disable fencing those few instructions — details differ per vendor, and the manual decides.

### 64-bit Atomics: On M0/M3, Mostly a Retreat to the Internal Lock

On 5.1's gauge, the 16-byte `atomic<Wide>` landed on the internal lock, while x86-64's int and long long were both lock-free. Move the same gauge onto a 32-bit M-profile core, and the locked membership grows a category. M-profile exclusive access comes in only three widths — byte, halfword, word — so 8-byte types have no matching instruction; the compiler settles for second best, tucking the operation into the runtime's internal lock. The interface doesn't change a bit, with the take-lock, change-value, release-lock sequence hidden behind the call — and `std::atomic<int64_t>`'s operations are no longer lock-free. M0 and M3 both sit on this road, the M0 tighter still, lacking even the 32-bit exclusive instructions. Judgment still belongs to the two inquiry points: run `is_lock_free()` once, and ask `is_always_lock_free` once at compile time.

The questioning method is the same set as 5.1's; here the gate presses on long long:

```cpp
// lock_free_probe.cpp —— 换一套工具链，同一套问法照用
#include <atomic>
#include <cstdio>

int main() {
  std::atomic<int> ai{0};
  std::atomic<long long> all{0};

  std::printf("atomic<int>: %s\n",
              ai.is_lock_free() ? "lock-free" : "uses lock");
  std::printf("atomic<long long>: %s\n",
              all.is_lock_free() ? "lock-free" : "uses lock");

  // 编译期的闸：long long 在这个平台上不是无锁的，直接编不过
  static_assert(std::atomic<long long>::is_always_lock_free,
                "int64 atomics must be lock-free on this platform!");
  return 0;
}
```

On the local x86-64 this compiles steadily, both prints reporting lock-free. Hand the same file to an ARM toolchain, and the readings and the gate's verdict follow the platform — as 5.1 said, when unsure ask it, don't memorize answers. I haven't measured the on-board readings; this section hands over the mechanism, and the numbers are your toolchain's to report.

### The ISR Warning: Lock-Backed Atomics Dead-Wait

Inside an interrupt service routine (ISR), don't touch lock-backed atomic operations — this one deserves its own paragraph. An ISR cannot block, while a lock-implemented atomic must take the lock before acting; the two together are a dead wait. On a single-core board this scene needs no staging: the main program's `fetch_add` is half done, the internal lock freshly taken, and an interrupt arrives; the ISR touches the same variable, and the runtime dutifully takes the lock first. Who holds the lock? The main program this very interrupt preempted — it can only release after the interrupt returns, while the ISR is stuck on taking the lock: the lock never comes, the interrupt never exits, and the whole system freezes on one `fetch_add`. This dead wait is derived from the mechanism; I haven't staged it on a board, and every step of the derivation lands on mechanism.

This article's protagonist holds a natural advantage here: `atomic_flag`'s `test_and_set` and `clear` are lock-free by standard guarantee — the operation layer passes through no lock. The exemption has a boundary: a spinlock built from it still has a lock holder. While the main program holds the lock and an interrupt arrives, the ISR's `test_and_set` itself still completes, but the while loop around it keeps spinning. The standard exempts the operation layer; lock-holding discipline is yours to keep.

So for atomic variables on the interrupt path, confirm they are truly lock-free before writing — the confirmation method is prepared in item 4 of the question list at this article's end: `is_always_lock_free` plus `static_assert`, stopping the locked specializations at compile time. This gate stands in the source, and every platform change or compiler-option change must pass it first.

## Your Turn to Try

Before putting pen to code, I line up the questions banked across these two articles; walk them against your own shared variables:

1. The business action you're protecting — written as memory actions, how many steps is it? Is each step whole on its own, and who guards the gaps between steps?
2. Can it be pressed into one read-modify-write? If you can, use fetch_add or a CAS loop; if not, the lock returns, or the design changes — don't force the press.
3. Is the CAS in your loop weak? Has the one-shot decision outside the loop switched to strong?
4. Is your atomic variable truly lock-free? One question to `is_always_lock_free` tells; press it down with static_assert on critical paths.
5. Have you swapped floating-point counting for integer minimum units? Do your custom type's fields carry padding, or a union?

Before turning the page, one hands-on exercise. Pull out 2.1's `atomic<int>` counter program and fix it two ways: one with fetch_add, one with a CAS loop that counts its retries inside the loop. Run 30 rounds and see whether both versions always come out whole, and where the CAS version's retry counts land.

## Reference Resources

The experiments' scene records, stated once. The two compile probes' replies are recorded verbatim above. The oversell demo's scene is staged, thread order fixed in code, readings reproducible. demo_flag's 200000, demo_float's integer control and stuck readings, and demo_init's zeros are identical on every run. The brackets in the text hold draft clause names; clause names match the official standard, and paragraph numbers may drift. cppreference's atomic_flag, fetch_add, constructor, and ATOMIC_FLAG_INIT pages were all fetched in full and read against the draft with no discrepancies. The progression follows Anthony Williams's *C++ Concurrency in Action*; the examples and prose are my own.

- [std::atomic_flag (cppreference)][cppref-flag], the lock-free guarantee, and C++20's test and wait/notify.
- [std::atomic's fetch_add (cppreference)][cppref-fetchadd], the C++20 notes for floating-point specializations and signed-integer wraparound.
- [std::atomic's constructors (cppreference)][cppref-ctor], default construction's value-initialization and LWG 2334.
- [ATOMIC_FLAG_INIT (cppreference)][cppref-afi], replaced by default construction since C++20; deprecation withdrawn by LWG 3659.
- [Class template atomic (current draft, atomics.types.generic)][draft-generic], the compile-time requirements like trivial copyability, the division of labor among the integer, floating-point, pointer, and smart-pointer specializations, and the note that the bool specialization goes through the primary template.
- [Operations on atomic types (current draft, atomics.types.operations)][draft-ops], CAS's effects and the spurious-failure note, the description of is_lock_free, and wait and notify.
- [Partial specializations for smart pointers (current draft, util.smartptr.atomic)][draft-smartptr], the atomic partial specializations of shared_ptr and weak_ptr.
- [Multi-threaded executions and data races (cppreference)][cppref-races], a recap of data-race determination — the clause verified in 2.1.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams], the reference for the progression.

[cppref-flag]: https://en.cppreference.com/w/cpp/atomic/atomic_flag
[cppref-fetchadd]: https://en.cppreference.com/w/cpp/atomic/atomic/fetch_add
[cppref-ctor]: https://en.cppreference.com/w/cpp/atomic/atomic/atomic
[cppref-afi]: https://en.cppreference.com/w/cpp/atomic/ATOMIC_FLAG_INIT
[draft-generic]: https://eel.is/c++draft/atomics.types.generic
[draft-ops]: https://eel.is/c++draft/atomics.types.operations
[draft-smartptr]: https://eel.is/c++draft/util.smartptr.atomic
[cppref-races]: https://en.cppreference.com/w/cpp/language/multithread
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
