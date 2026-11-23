---
title: "The Four Tiers of Memory Order: From Sequential Consistency to relaxed"
description: "From seq_cst's machine-wide order to relaxed's atomicity-only, the middle tiers — release and acquire, acq_rel, and the deprecated consume — each explained tier by tier: what is guaranteed, what isn't, and what each costs."
chapter: 5
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [20, 26]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter05-atomic-memory-model/04-choosing-memory-orders.md
  source_hash: a29b83918c206d06398d5f47dfdf91374a85ff6379507230ee6ca28d38b4c35a
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 10080
---

# The Four Tiers of Memory Order: From Sequential Consistency to relaxed

Let's set the position up front: this article gives criteria, not memorized answers. Memorize six parameter tiers and a new scene still leaves you blank. There is only one criterion — the bridge 5.3 stood up. The reads and writes you're checking need a bridge; the memory order's task is arranging the bridge's two ends and the goods upon it. Which tier to pick depends on what the bridge must connect.

A note on the experiment readings: I ran the litmus three tiers twice each, readings all written into the text; the multithreaded readings are all single-run readings.

## Starting from Sequential Consistency

The most expensive and easiest-to-reason-about of the six tiers is the default; start there. seq_cst gives loads acquire, stores release, and read-modify-writes both; stacked on those three is one more: all seq_cst operations line up in one total order, and all threads see the same one ([atomics.order] paragraph 4). In plain words, the global seq_cst actions line up on a single line, and on who precedes whom, everyone's view agrees.

The total order's use, seen in the shortest example 5.2 left behind: thread one does `x.store(1)` then `y.store(2)`; thread two reads y then x. Relax the reads to relaxed and thread two may read y as 2 with x as 0 — each variable whole on its own, yet jointly mismatched. Switch to seq_cst, and what blocks it is the total order. 5.2 only stated the qualitative claim; 5.3's litmus measured it: zero hits in ten million rounds.

The draft adds a reminder in a note, relayed verbatim (the note at [atomics.order] paragraph 7): seq_cst guarantees sequential consistency only for programs without data races where all atomic operations use seq_cst; mix in any weaker usage and the guarantee is void, unless you are exceptionally careful. I read it this way: the default tier makes reasoning cheap because it manages the ordering of all your atomic variables together. Downgrade, and the portion it was managing becomes yours to take over. The sentence 5.3 left for this article when discussing strongly happens before — that the threshold sits on seq_cst at the bridge's two ends — becomes clear once you've seen the total order and then the next section's paired bridge: that is why the threshold stands where it does.

When to stay on the default tier? My criterion has two conditions; meeting either keeps you there. If several atomic variables in your hand mesh with one another, none dispensing with the others' order, seq_cst is the standard's only tier granting a total order. Or, if you cannot count a bridge in your code or say clearly which read/write pair must connect, staying on the default keeps your retreat open — first keep it correct, then talk about saving.

## release and acquire: Building the Bridge Where the Pairing Happens

For the middle tiers, start with release and acquire. release goes on stores: the reads and writes before it may not be moved after it. acquire goes on loads: the reads and writes after it may not be hoisted before it. When the reading side takes the value a release store wrote, the two sides synchronizes-with and the bridge connects at the pairing point ([atomics.order] paragraph 2): every operation before the store-side's release is visible to every operation after the load-side's acquire.

A handshake only needs two threads' actions to meet; a global total order is money wasted on the harness. The tier that bridges at the pairing point is exactly enough — that is acquire and release's position: pay for the bridge only where a bridge is needed.

Saying it isn't enough — let's measure. I carried over 5.3's litmus harness, packed the action pair into its own function, and made the memory order a compile-time three-way choice: one tier relaxed, one release paired with acquire, one seq_cst.

<OnlineCompilerDemo
  title="litmus in Three Tiers: relaxed, release with acquire, seq_cst"
  source-path="code/examples/vol5/chapter05/13_litmus_acqrel.cpp"
  description="The memory order is a compile-time three-way choice; the default -DORDER_MODE=0 is the relaxed tier, hitting tens of thousands of times. Recompile with 1 or 2 (commands and readings below)."
  run-options="-O2 -std=c++20 -pthread -DORDER_MODE=0"
  allow-run
>

```cpp
// litmus_acqrel.cpp —— 两条线程互测 x、y 两端的读写次序，内存序编译期三选一
// 架子与 5.3 的 litmus 相同：放行哨兵、应答只增不减、重置压在放行之前、绑核 0 与 8
// 用法: g++ -std=c++20 -O2 -pthread -DORDER_MODE=0|1|2 litmus_acqrel.cpp
// 0 = relaxed   1 = release/acquire   2 = seq_cst
#include <atomic>
#include <cstdio>
#include <pthread.h>
#include <thread>

#if ORDER_MODE == 0
#define SORD std::memory_order_relaxed
#define LORD std::memory_order_relaxed
#define NAME "relaxed"
#elif ORDER_MODE == 1
#define SORD std::memory_order_release
#define LORD std::memory_order_acquire
#define NAME "release/acquire"
#elif ORDER_MODE == 2
#define SORD std::memory_order_seq_cst
#define LORD std::memory_order_seq_cst
#define NAME "seq_cst"
#endif

struct alignas(64) Slot { std::atomic<int> v{0}; };   // x、y 各住一条缓存行
static Slot sx, sy;
static std::atomic<int>& x = sx.v;
static std::atomic<int>& y = sy.v;

// 动作对单独装一个函数，objdump 对照三版指令时一眼可见
__attribute__((noinline)) int probe(std::atomic<int>& w, std::atomic<int>& r) {
  w.store(1, SORD);
  return r.load(LORD);
}

struct alignas(64) Hand {
  std::atomic<long> round{1};   // 放行哨兵，主线程每轮推一号，唯一的起跑口
  std::atomic<long> ack{0};     // 应答，2r 表示读到 1，2r+1 表示读到 0，只增不减
};
static Hand ha, hb;

static void pin(std::thread& t, int cpu) {   // glibc 绑核接口（_np，不可移植），固定实验条件用
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  pthread_setaffinity_np(t.native_handle(), sizeof(set), &set);
}

int main() {
  const long kTrials = 10000000;
  long hits = 0;

  std::thread ta([&] {
    for (long r = 1; r <= kTrials; ) {
      if (ha.round.load(std::memory_order_relaxed) < r) continue;
      int got = probe(x, y);              // 写 x，读 y
      ha.ack.store(2 * r + (got == 0 ? 1 : 0), std::memory_order_release);
      ++r;
    }
  });
  std::thread tb([&] {
    for (long r = 1; r <= kTrials; ) {
      if (hb.round.load(std::memory_order_relaxed) < r) continue;
      int got = probe(y, x);              // 写 y，读 x
      hb.ack.store(2 * r + (got == 0 ? 1 : 0), std::memory_order_release);
      ++r;
    }
  });

  pin(ta, 0);
  pin(tb, 8);

  for (long r = 1; r <= kTrials; ++r) {
    if (r > 1) {   // 上一轮应答收齐才开下一轮，重置压在放行之前
      while (ha.ack.load(std::memory_order_acquire) < 2 * (r - 1)) {}
      while (hb.ack.load(std::memory_order_acquire) < 2 * (r - 1)) {}
    }
    x.store(0, std::memory_order_relaxed);
    y.store(0, std::memory_order_relaxed);
    ha.round.store(r, std::memory_order_seq_cst);
    hb.round.store(r, std::memory_order_seq_cst);
    long aa = 0, ba = 0;
    while ((aa = ha.ack.load(std::memory_order_acquire)) < 2 * r) {}
    while ((ba = hb.ack.load(std::memory_order_acquire)) < 2 * r) {}
    if ((aa & 1) && (ba & 1)) ++hits;   // 两边同时读到旧值 0 才记命中
  }

  ta.join();
  tb.join();
  std::printf("%s: %ld of %ld rounds saw r1=0 and r2=0\n", NAME, hits, kTrials);
}
```

</OnlineCompilerDemo>

The readings are laid out below, two runs per tier, all single-run readings:

```text
relaxed: 89258 of 10000000 rounds saw r1=0 and r2=0
release/acquire: 60782 of 10000000 rounds saw r1=0 and r2=0
seq_cst: 0 of 10000000 rounds saw r1=0 and r2=0

relaxed: 63698 of 10000000 rounds saw r1=0 and r2=0
release/acquire: 70389 of 10000000 rounds saw r1=0 and r2=0
seq_cst: 0 of 10000000 rounds saw r1=0 and r2=0
```

Lay the readings side by side: the middle tier sits in the same order of magnitude as the fully relaxed one, sixty to ninety thousand hits per ten million rounds, while the total-order tier reads zero as before. The machine code lays out the reason plainly — the probe function of all three tiers side by side (objdump output, address column trimmed):

```text
relaxed:         movl $0x1,(%rdi)     ← 写
                 mov  (%rsi),%eax     ← 读

release/acquire: movl $0x1,(%rdi)     ← 写，与 relaxed 一条不差
                 mov  (%rsi),%eax     ← 读

seq_cst:         mov  $0x1,%eax
                 xchg %eax,(%rdi)     ← 写换成 xchg，当场生效
                 mov  (%rsi),%eax     ← 读
```

Look at the instructions. On x86, release and acquire compile to exactly what relaxed does; the compiler only blocks one more class of compile-time reordering. The scene the litmus measures is x and y meshing with each other, and both sides reading the old value simultaneously requires precisely global ordering — acquire and release establish no global order, so the readings naturally land in the relaxed tier's magnitude. Zero hits is what you want, and `xchg` is the price you pay.

5.2's unpaid bill we settle here too. `atomic_flag`'s `test_and_set` carries acquire and `clear` carries release — exactly what 2.1's hand-rolled spinlock used: for the lock taker, reads and writes after the lock may not be hoisted before `test_and_set`; for the releaser, the critical section's writes are pressed before `clear`. The bridge's shape is identical to the `std::mutex` one counted in 5.3, only the part changed from a lock to an atomic flag. The several acquires and releases in 2.1's and 5.2's code, plus the last row of 5.3's synchronization table — the judgment method is already familiar: are the bridge's two ends them, and did the reading side take the writing side's value?

One more detail worth standing up. What the reading side takes need not be the release store itself — read-modify-writes queued after it, by anyone, also count; the standard calls it the **release sequence** ([atomics.order] paragraph 2's original words are "takes its value from the release sequence headed by A"). The concept of the release sequence we stand up right in this section; the cookbook elective *Introduction to Lock-Free* will use it again and again — not yet written.

## acq_rel: Both Ends of a Read-Modify-Write

Let's make **read-modify-write** clear (read the old value, compute the new, write it back, in one action): it holds a read and a write at once, and both ends can take an order. acq_rel occupies both ends: as a read it is acquire, catching the upstream bridge; as a write it is release, opening the downstream bridge. cppreference's constant table writes the two sentences side by side, and the standard's original text at [atomics.order] paragraph 1 names it in both the acquire and the release lists.

The criterion is still counting bridges, only now count both ends. When you write a `fetch_sub`, ask two questions: whose release is its read waiting for, and to whom does its write release? Both ends chained — use acq_rel. One end only — pick acquire or release. Neither end — relaxed. Reference-count decrements are the model of a both-ends chain, expanded in 5.5's reference-counting section.

A floor for the cost: on x86, a read-modify-write compiles to the same `lock xadd` whatever the memory order — the instruction comparison comes in the table further on. The gap only shows on weakly ordered machines, whose readings this article cannot give, and I won't pretend to have measured them.

## relaxed: Scenes Where Atomicity Alone Is Enough

relaxed's promise list is the shortest, and that is exactly where its value sits; take the items one by one. First, actions are whole — no other thread sees a half-finished product. Second, modifications of the same variable are seen by all threads in one and the same order; the standard calls this the **modification order**, and the four consistency requirements apply to all atomic operations, with relaxed on the beneficiary list too. What it doesn't give is the bridge — not one section of it.

Let's measure its floor with a counter:

<OnlineCompilerDemo
  title="A relaxed Counter: Atomicity Alone Is Still Exact"
  source-path="code/examples/vol5/chapter05/14_counter_relaxed.cpp"
  description="Four threads each add a million times; the relaxed reading is 4000000. The number itself is the goal, with no goods beside it needing the bridge — this is its scene."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// counter_relaxed.cpp —— relaxed 只保原子性：四条线程各加 100 万次
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic<long long> served{0};

void work(int rounds) {
  for (int i = 0; i < rounds; ++i) {
    served.fetch_add(1, std::memory_order_relaxed);   // 只保原子性，不搭桥
  }
}

int main() {
  std::thread ts[4];
  for (int i = 0; i < 4; ++i) ts[i] = std::thread(work, 1000000);
  for (auto& t : ts) t.join();
  std::printf("4 条线程各加 100 万次，relaxed 读数 %lld\n", served.load(std::memory_order_relaxed));
}
```

</OnlineCompilerDemo>

```text
4 条线程各加 100 万次，relaxed 读数 4000000
```

Four threads adding a million each, exact to the digit — relaxed's floor is measured. Its fitting scenes are clear too: the number itself is the goal, with no goods beside it that others await. Statistics counters, task counts that only report progress, reference-count increments — all land here. cppreference's relaxed section gave examples naming the typical use as incrementing counters, such as `std::shared_ptr`'s reference count, and the same page immediately reminds that decrementing the count needs acquire-release to accompany destruction — the treatment genuinely differs, and the criterion is counted in 5.5's reference-counting section.

The relaxed mentioned when 1.6 discussed stop flags — 5.3 counted its bridge as not one section built — we now set the boundary straight: the flag itself doesn't break under relaxed; the action stays whole. What breaks is the data beside it that should become visible together; whoever sees the stop usually goes on to read configuration, and the configuration not being on the bridge means what's read may be stale. The fix is adding the bridge, delivered in 5.5's stop-flag section; this article's demo is written with the fix, and the relaxed control group is left for the hands-on exercise at 5.5's end.

## The Current State of consume: A Deprecated Dependency Order (Extension)

This section carries the (extension) mark; skipping it does not affect the main line. Take away exactly one thing: don't write `memory_order_consume`.

consume's original abacus was finer than acquire's; lay it open. acquire is a full subscription — after the load, all reads and writes are barred from moving earlier. consume wanted to constrain only the operations depending on the loaded value. Pointer publication was its imagined home field: the reader takes the pointer, and the data accessed through the pointer is ordering-protected, while accesses unrelated to the pointer go unconstrained — in theory, not one cent wasted.

Reality didn't cooperate; let's say it straight. To honor a dependency order, the compiler must track value flow across the entire function; the moment a variable is renamed or a value lands in a register, the dependency chain snaps mid-way, and a snapped chain must have its ordering widened, or errors follow. No mainstream compiler implements its exact semantics — in practice, all run the name consume at acquire's strength. The committee's disposition proposal is numbered P3475R2, and the verb in its title is blunt: defang and deprecate — pull the semantics, then mark it deprecated.

The current state, recorded by date, one sentence per source. cppreference's entry table, which I fetched on 2026-11-16, marks `memory_order_consume` deprecated in C++26. The companion `kill_dependency` was deprecated in the same batch. The two formal definitions — carries dependency and dependency-ordered before — are both marked until C++26 in the same table. I also fetched eel.is's current working draft: [atomics.order]'s enumeration holds only five values — relaxed, acquire, release, acq_rel, seq_cst — and consume is no longer among them. libstdc++ merged the corresponding deprecation patch in May 2026. Which future version deletes the name outright I have not found settled; worth re-checking when you read this.

The criterion in one sentence: to express "load the pointer, then use it", acquire is today's spelling — semantics only stronger, never weaker, and behavior uniform across all compilers. If consume still lives in your codebase, swap it for acquire.

## Reference Resources

The experiments' readings, stated once. The litmus three tiers I ran twice each, all single-run readings; an earlier round of pre-runs covered the same three tiers (relaxed, release with acquire, seq_cst) with the same orders of magnitude and zero seq_cst hits throughout. counter_relaxed's 4000000 reads identically on every run. The instruction comparison comes from objdump, address column trimmed. The eel.is draft used for assertion checking was fetched on 2026-11-16; the clause names are all carried in the text, and paragraph numbers drift with draft versions. [atomics.order] was checked for the enumeration, the release/acquire synchronization relationship, the release sequence, and the seq_cst total order with the mix-in-weaker note. consume's deprecation status was verified in two places: cppreference's entry-table C++26 marking, and libstdc++'s merge record. The progression follows Anthony Williams's *C++ Concurrency in Action*, taking its unit arrangement and example-design thinking; the examples and prose are my own.

- [std::memory_order (cppreference)][cppref-mo], the six-tier constant table, consume's C++26 deprecation marking, and relaxed's typical uses.
- [Current standard draft: Order and consistency (atomics.order)][draft-atomics-order], the five-tier enumeration, the synchronization relationship, the release sequence, and the seq_cst total order with the mix-in-weaker note.
- [libstdc++'s patch deprecating memory_order_consume (GCC mailing list)][gcc-consume], merged May 2026.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams], the reference for the progression.

[cppref-mo]: https://en.cppreference.com/w/cpp/atomic/memory_order
[draft-atomics-order]: https://eel.is/c++draft/atomics.order
[gcc-consume]: https://gcc.gnu.org/pipermail/gcc-patches/2026-May/718127.html
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
