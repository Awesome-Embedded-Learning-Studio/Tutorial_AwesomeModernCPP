---
title: "How One Thread Observes Another's Modifications"
description: "From compiler and CPU reordering to cache coherence, happens-before explained as a bridge you can inspect section by section, then used to re-prove the earlier locks, joins, and condition variables."
chapter: 5
order: 3
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
translation:
  source: documents/vol5-concurrency/chapter05-atomic-memory-model/03-happens-before.md
  source_hash: acd8652035de4f41a461d0a0b62d5ec577d363a869819d4e559ebe476dbb34a8
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 16068
---

# How One Thread Observes Another's Modifications

Two threads, one writing and one reading; the write comes first, the read after — by what right does the reading side necessarily see the new value?

Single operations we have already handed to `std::atomic`: one read, one write, one read-modify-write, each step whole, with no other thread seeing a half-finished product. But "each step whole" alone is not enough. When Unit 2 ruled on data races ([*Same Counter, Two Threads — Why Does It Compute Wrong?*](../chapter02-sharing-data/01-from-wrong-programs)), one of the four questions I left owing until now: between the two actions, neither happens-before the other. happens-before is the standard's relation governing "who comes before whom"; this article explains it as a bridge you can inspect section by section.

A note on the experiment readings: the multithreaded readings are all single-run readings. The locked counter and the infinite-loop program's `-O0` tier, I re-ran twice each, and the readings are written into the text. Clause citations carry clause names; paragraph numbers drift with draft versions.

## Why Reordering Happens: Compiler and CPU

Start with the compiler. The program below does one thing: a worker thread repeatedly checks a `bool`, and the main thread flips it to `true` after 200 ms.

<OnlineCompilerDemo
  title="A Plain bool as Stop Flag: -O0 Ends Fine, -O2 Loops Forever"
  source-path="code/examples/vol5/chapter05/11_stop_bool.cpp"
  description="Compiled at -O0, it exits normally after 200 ms and prints the spins line. Recompiled at -O2, the worker thread never comes out. For demonstration: the cross-thread reads and writes on stop are a data race."
  run-options="-O0 -std=c++20 -pthread"
  allow-run
>

```cpp
// stop_bool.cpp —— 普通布尔做停止标志：-O0 正常收场，-O2 工作线程出不来
// 演示用：stop 上的跨线程读写是数据竞争，标准已不作要求，别当写法抄
#include <chrono>
#include <cstdio>
#include <thread>

bool stop = false;   // 故意不用 std::atomic<bool>，本节只做演示

int main() {
  long long spins = 0;
  std::thread t([&] {
    while (!stop) ++spins;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  stop = true;
  t.join();
  std::printf("main set stop, thread saw it, spins=%lld\n", spins);
}
```

</OnlineCompilerDemo>

Compiled at `-O0`, the program ends normally. I ran it twice; the worker turned 350-odd million and 520 million laps respectively, both saw `stop` flip, printed one line, and exited. The same source at `-O2`, and the worker never comes out. I ran it under `timeout 2`: two seconds later the process was killed, exit code 124, and the `spins` line never appeared.

The machine code says it straight. Under `-O2`, the worker's entire function is (objdump output, with the address column and symbol mangling trimmed by me):

```text
cmpb $0x0,0x2d0a(%rip)   # 4071 <stop>    ← stop 只在这里读一次
jne  1378                                 ← 已经是 true，函数返回
jmp  1370                                 ← 否则跳回自己，循环体整个没了
```

The compiler hoisted the read out of the loop and, in passing, deleted `++spins` clean away. In its eyes, if the loop never stops, nobody can ever read `spins`' value; if the loop does stop, `stop` was already `true` at entry, and any number of increments would be wasted. It isn't lying. The standard only requires implementations to simulate the abstract machine's observable behavior ([intro.abstract] paragraph 1; a footnote names it the **as-if rule**): transformations invisible to a single thread, it is entitled to make. Bring in a second thread and you see it instantly — the worker never looks at `stop` again.

A fair word too. This is itself a data race, on which the standard makes no requirements at all ([intro.races] paragraph 17); the compiler proceeded on "no other thread writes stop", and its reasoning is entirely legal. Take it as a counter-example, not as a pattern.

Now look at the CPU side's reordering, which hides better. A modern processor executes dozens of instructions at once; whoever's operands arrive first goes first. Writes have an even more conspicuous destination: when a write instruction reaches the core, its value doesn't become visible to other cores immediately — it queues in the **store buffer** (each core's own small queue for writes not yet effective externally). Writes pressed into the buffer haven't taken effect, yet a later read on the same core may walk right past it, and the reader never knows an uncommitted write sits ahead of it.

x86-64's memory model goes by a common name, **TSO** (Total Store Order): write-write order kept, read-read order kept, with one crossing permitted — one's own later read passing one's own queued write. On weakly ordered machines like ARM, all four orderings can flip. The same code producing different readings on the two sides is exactly why the standard builds a memory model of its own: the language must offer platform-independent promises, and we cannot stake correctness on one hardware vendor's temperament.

Reasoning alone isn't enough — let's actually measure. Two atomic variables, `x` and `y`, both starting from 0. Thread a writes `x`, then reads `y`, recording what it got. Thread b does the mirrored pair with `y` and `x`. Each side's "write first, read after" is fixed; what's being tested is whether both sides can simultaneously read the other's old value 0. Experiments of this kind — two threads mutually probing read/write order — the memory-model community calls a **litmus test**, and our file is named litmus.cpp.

The full program spends ink in two places, which I'll account for. One is the start alignment: the two threads must set off at the same instant, so I use a per-round incrementing release sentinel that has them spin until the same round before acting. The other is the settle-and-reset order: the main thread opens the next round only after collecting both sides' acknowledgments, with each round's reset pressed before the release. None of it is arbitrary — a troubleshooting passage later returns here. The harness's own handshake also bridges with atomics, pairing release with acquire; in the happens-before table below, the last row is exactly them. The handshake is not the object under test — what's tested throughout is the `x` and `y` ends. Which memory order to choose belongs to 5.4.

<OnlineCompilerDemo
  title="litmus: Two Threads Mutually Probing the Read/Write Order of x and y"
  source-path="code/examples/vol5/chapter05/12_litmus.cpp"
  description="Compiled by default at the seq_cst tier, ten million rounds with zero hits. Recompile with -DUSE_RELAXED=1 per the command below to see hits; both readings are recorded in the text."
  run-options="-O2 -std=c++20 -pthread -DUSE_RELAXED=0"
  allow-run
>

```cpp
#include <atomic>
#include <cstdio>
#include <pthread.h>
#include <sched.h>
#include <thread>

#ifndef USE_RELAXED
#define USE_RELAXED 0
#endif
constexpr bool kRelaxed = USE_RELAXED;

struct alignas(64) Slot { std::atomic<int> v{0}; };   // x、y 各住一条缓存行
static Slot sx, sy;
static std::atomic<int>& x = sx.v;
static std::atomic<int>& y = sy.v;

struct alignas(64) Hand {
  std::atomic<long> round{1};   // 主线程每轮推一号，是唯一放行口
  std::atomic<long> ack{0};     // 回 2r（读到 1）或 2r+1（读到 0），只增不减
};
static Hand ha, hb;

static void pin(std::thread& t, int cpu) {   // glibc 的绑核接口（_np，不可移植），拿来固定实验条件
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  pthread_setaffinity_np(t.native_handle(), sizeof(set), &set);
}

int main() {
  const auto ord = kRelaxed ? std::memory_order_relaxed
                            : std::memory_order_seq_cst;
  const long kTrials = 10000000;
  long hits = 0;

  std::thread ta([&] {
    for (long r = 1; r <= kTrials; ) {
      if (ha.round.load(std::memory_order_relaxed) < r) continue;
      x.store(1, ord);
      int got = y.load(ord);
      ha.ack.store(2 * r + (got == 0 ? 1 : 0), std::memory_order_release);
      ++r;
    }
  });
  std::thread tb([&] {
    for (long r = 1; r <= kTrials; ) {
      if (hb.round.load(std::memory_order_relaxed) < r) continue;
      y.store(1, ord);
      int got = x.load(ord);
      hb.ack.store(2 * r + (got == 0 ? 1 : 0), std::memory_order_release);
      ++r;
    }
  });

  pin(ta, 0);   // 0 号与 8 号逻辑核，本机拓扑分属物理核 0 与 4
  pin(tb, 8);

  for (long r = 1; r <= kTrials; ++r) {
    if (r > 1) {   // 上一轮的应答收讫，才轮到重置与放行
      while (ha.ack.load(std::memory_order_acquire) < 2 * (r - 1)) {}
      while (hb.ack.load(std::memory_order_acquire) < 2 * (r - 1)) {}
    }
    x.store(0, std::memory_order_relaxed);
    y.store(0, std::memory_order_relaxed);
    ha.round.store(r, std::memory_order_seq_cst);   // 放行，顺带把重置压出去
    hb.round.store(r, std::memory_order_seq_cst);
    long aa = 0, ba = 0;
    while ((aa = ha.ack.load(std::memory_order_acquire)) < 2 * r) {}
    while ((ba = hb.ack.load(std::memory_order_acquire)) < 2 * r) {}
    if ((aa & 1) && (ba & 1)) ++hits;
  }

  ta.join();
  tb.join();
  std::printf("%s: %ld of %ld rounds saw r1=0 and r2=0\n",
              kRelaxed ? "relaxed" : "seq_cst", hits, kTrials);
}
```

</OnlineCompilerDemo>

Two compile commands for you; the memory order is chosen at compile time:

```bash
g++ -std=c++20 -O2 -pthread -DUSE_RELAXED=1 litmus.cpp -o litmus_relaxed
g++ -std=c++20 -O2 -pthread -DUSE_RELAXED=0 litmus.cpp -o litmus_seqcst
```

In the output, r1 is thread a's reading of `y`, r2 is thread b's reading of `x`; only both being 0 together counts as a hit. The readings are below, two runs each, all single-run readings:

```text
relaxed: 88260 of 10000000 rounds saw r1=0 and r2=0
seq_cst: 0 of 10000000 rounds saw r1=0 and r2=0

relaxed: 89054 of 10000000 rounds saw r1=0 and r2=0
seq_cst: 0 of 10000000 rounds saw r1=0 and r2=0
```

The relaxed group saw both sides read the old value a few dozen to a hundred times per ten thousand rounds. The two official runs landed at 88 and 89; later re-runs landed between 88 and 137, the order of magnitude steady. Put the readings onto the machine and the shape fits exactly: each thread's write sat uncommitted in its own store buffer, and each read walked past its own queued write, fishing back the other's 0. The one crossing TSO permits is precisely what was measured. The seq_cst group: zero hits in a full ten million rounds. Not luck — the compiler encoded the seq_cst store as a different instruction; put the two compilations side by side (objdump excerpt, thread b's loop):

```text
// relaxed：写是一条普通 mov，随后直接读另一个变量
movl   $0x1,0x2c2d(%rip)        # sy     ← 写
mov    0x2c65(%rip),%ecx        # sx     ← 读

// seq_cst：写换成 xchg，自带全屏障
xchg   %esi,0x2c2c(%rip)        # sy     ← 写，当场生效
mov    0x2c66(%rip),%edx        # sx     ← 读
```

`xchg` carries a full fence on x86: the write doesn't wait for the buffer, takes effect for all cores on the spot, and the subsequent read naturally cannot slip past. Put the two ends' readings together and you see it — order is not a free gift of time; someone must spend instructions to stand it up.

Daring to lay the readings out like this hides a troubleshooting story I want to leave for you. In the first few versions of the harness, the seq_cst group also reported hits — tens of thousands in some runs, millions in others, different every round. The xchg full fence commits writes on the spot, the instruction shape was just seen, and both sides reading 0 simply cannot be staged under the local instruction shape; readings and instruction shape had locked horns, so the fault could only lie in my harness. The language layer holds one more universal truth: all seq_cst operations exist in a single total order ([atomics.order] paragraph 4) — one sentence of characterization suffices here, with the details in 5.4. Digging it out: the previous round's release gate took effect before the next round's reset, so the 0 a thread read could be the main thread's later-committed reset, not at all "the other side hasn't written". Collapsing the release into the single exit and pressing the reset before the release made the false positives vanish. When readings and the model disagree, the thing worth suspecting is the harness, not the model.

A word on pinning too. I fixed the two threads on logical cores 0 and 8 (glibc's `pthread_setaffinity_np` — the _np marks a non-portable interface, used here only to fix experimental conditions). The local machine is an AMD 8-core 16-thread part with logical cores enumerated in pairs: `/sys/devices/system/cpu/cpu0/topology/thread_siblings_list` reads 0-1, cpu8 reads 8-9, so 0 and 8 belong to physical cores 0 and 4. Before pinning, it's worth reading your own machine's sibling info under /sys. Two logical cores of one physical core share a store buffer; pinning them apart is what measures the inter-core window accurately.

## Cache Coherence Background: MESI

In the experiments above, the two threads read and wrote two separately placed atomic variables. For "writes will eventually be seen" to hold, another layer must be in charge underneath: the per-core caches and the coherence protocol between them. This section is the plain-words version of the hardware background; the protocol state machines differ across vendors, and we take only the trunk.

Each core owns a private cache; data moves in cache lines, commonly 64 bytes on x86-64. When a core changes a number, it changes its own cached copy, while other cores still clasp old copies. It sounds like anarchy, but it can't descend into it: cache lines run a coherence protocol between them, the most common family called **MESI**, named for the initials of four states: Modified, Exclusive, Shared, Invalid. The plain-words version of the four states:

| State | Plain words | Incidental rule |
|------|------|-----------|
| Modified | I just wrote it; the newest value machine-wide is with me, and memory holds the old | Only I hold it; others wanting the latest must come to me |
| Exclusive | Only I have it cached, consistent with memory | Write whenever I like; writing upgrades it to Modified |
| Shared | Several cores may hold it, all consistent with memory | Whoever wants to write invalidates everyone else's copy |
| Invalid | My copy is voided, as good as gone | To read or write, request it again |

The flow of one write, in plain words: the would-be writer sends invalidation messages, waits for all the receipts, upgrades itself to Modified, and then puts pen to page. From then on, whoever reads it — as long as they lack the newest copy — must take it from the Modified core. So "writes eventually become visible" is governed in protocol prose: for writes to the same address, all cores see one and the same order, and nobody reads a value that skips a write.

The standard itself names it: a note under the memory-model clauses says the consistency rules deliver the cache-coherence guarantees most hardware provides into the hands of C++ atomic operations (the note at [intro.races] paragraph 15). We can see it: the language layer and the hardware layer align on visibility.

Visible is one thing; timely is another. While a write sits pressed in its own core's store buffer, other cores see nothing. Invalidation messages fly in transit, and receipts must be awaited. The litmus relaxed group's dozens-to-hundreds per ten thousand rounds measures exactly the width of that window. The protocol promises "eventually consistent"; it never promises "arrives the next instant" — and when you later benchmark concurrent programs, that distinction will surface again and again.

One more distinction to make. Coherence governs the order of values read within one address (one cache line); it does not constrain which of two different addresses comes first. The litmus test is precisely the proof: `x`'s and `y`'s writes both became "eventually visible", yet the two sides' observed orders still disagreed. Cross-address order, hardware doesn't deliver — you must ask the language layer for it, and the asking place is the next section.

## happens-before: Order Established by Synchronization

Start inside one thread. The standard sets an order within a single thread called **sequenced-before** ([intro.execution] paragraph 8): if evaluation A sequenced-before evaluation B in the same thread, A must complete before B. Asymmetric and transitive, it is the part of program order the language truly guarantees. When you write `data = 42; ready = true;`, what stands between the two statements is exactly it.

The cross-thread bridge is called **synchronizes-with**. When particular library calls appear in pairs, one side establishes a synchronization relationship with the other ([intro.races] paragraph 6). The standard can count the bridging occasions; the four we use in this article:

| Bridging action | Establishes synchronization between | Clause |
|------------|------------------|------|
| Unlock paired with lock: `m.unlock()` and a subsequent successful `m.lock()` on the same mutex | The prior unlock and that lock acquisition | [thread.mutex.requirements.mutex]/8, /24 |
| `std::thread` construction completes | The start of the new thread's function | [thread.thread.constr]/7 |
| Thread termination | The return of a successful `join()` | [thread.thread.member]/4 |
| A release store | An acquire load that reads the value it stored | [atomics.order]/2 |

Join the two and you get the overall relation **happens-before** ([intro.races] paragraph 7): A happens-before B if and only if A sequenced-before B, or A synchronizes-with B, or there is an intermediate X with A happens-before X and X happens-before B. Transitivity is exactly what governs this — bridges may be built one after another, and as long as they connect all the way through, head and tail are related.

Its usefulness lands in two clauses; take them apart. One is visibility: if write A happens-before read B with no other write cutting in between, what B reads is the value A wrote ([intro.races] paragraph 9, which calls it a visible side effect). The other is adjudication: two potentially concurrent conflicting actions, neither happens-before the other, and at least one of them non-atomic — the execution then contains a data race, with undefined behavior as the consequence (paragraph 17). Unit 2 quoted the second half of that ruling.

The plain-words criterion — this is how we interrogate code. Between a possibly concurrent read/write pair, you must be able to point at the complete bridge in the code: write sequenced-before unlock (or release store); unlock synchronizes-with the other side's lock (or acquire load); lock sequenced-before read. Three sections joined into one, and missing any one breaks it. Conversely, if you cannot count the bridge, don't gamble: either add synchronization, or the program is already outside the standard's jurisdiction and running correctly counts only as a sample.

As for relaxed, we have already seen its face in the litmus test: every step whole, not one bridge section built, and both sides reading 0 in perfect order. The clause defines it as "no memory ordering" ([atomics.order] paragraph 1), yet it remains an atomic operation and generates no data race (the note at [intro.races] paragraph 3). Which order an atomic must carry to build a bridge, and how much of one, belongs to 5.4, where we continue the comparison. The relaxed stop flag from [*Ending Work in an Orderly Way*](../chapter01-start-concurrency/06-jthread-and-stopping) doesn't even guarantee visibility — count it by the criterion once and it's clear: flag relaxed, not one bridge section built.

One promise left to honor. [*Choosing Tools by What the Coordination Needs*](../chapter03-wait-and-coordinate/07-choosing-tools) padded a sentence about "strongly happens before" with the barrier's completion function, calling it a formal memory-model relation for Unit 5 to cover. We are now in Unit 5, so let's set it straight. **Strongly happens before** is a tighter relation than happens-before ([intro.races] paragraph 8): sequenced-before within a thread counts as strongly happens before; across threads, both ends of the bridge must be seq_cst atomic operations for the relation to apply — or a whole run of "same-thread order, bridge, same-thread order again" also qualifies. The barrier's completion step is set by the standard at exactly the sturdier tier: arrive strongly happens before the current phase's completion step begins ([thread.barrier.class]/14), and the completion step's end strongly happens before the released threads return (same clause, /3). 3.7's plain words still stand: anything written in the completion step is readable after release, and the completion function reads the data threads just wrote without locking, leaning precisely on the tighter promise. Where do strongly-happens-before and happens-before usually differ? At the seq_cst threshold on the bridge — and what lies behind that threshold belongs to 5.4.

## Re-understanding the Earlier Locks and Condition Variables with happens-before

The parts are assembled; let's turn back and re-prove the earlier programs. There is exactly one proof method: count the bridges.

### Why the Locked Counter Loses Nothing Across 30 Rounds

[*How Do We Get the Lost Counts Back? — std::mutex and the Three Scoped Locks*](../chapter02-sharing-data/02-mutex-and-locks) gathered the count-losing counter into a lock, 30 rounds all correct. I re-ran that same reading twice on 2026-11-16, both 30/30, compiled back at `-O0` as the original was. Count the chain section by section: thread A executes `++counter` inside the critical section; the write sequenced-before the `unlock()` closing that same critical section — program order. Then one bridge section: between `unlock()` and thread B's subsequent successful `lock()`, the standard writes synchronizes-with ([thread.mutex.requirements.mutex]/24). B's locking sequenced-before its read. Three sections joined: A's write happens-before B's read.

Why does every link-up succeed? Look one step deeper into the lock: all lock and unlock actions line up in a single total order (same clause, /4). Two threads at 160000 each make 320000 critical sections, strung along it into one long chain; every time B enters a critical section, every value A wrote before is visible to it. Before the main thread's closing read of `counter`, `a.join()` and `b.join()` pad both worker threads' entire writes ahead of it ([thread.thread.member]/4).

Look back, and the reason 30 rounds lose nothing lands on the chain: the bridge was there the whole way.

### The Program That Looks Right at -O2: Which Section Is Missing

*Same Counter, Two Threads — Why Does It Compute Wrong?*'s unlocked counter came out correct 30 rounds at `-O2`. You can now re-adjudicate in chain language: between the two workers' writes and reads of `counter` there is no lock, `counter` is not atomic, and not one bridge section can be counted. join doesn't help — it connects each worker to the main thread, never to each other. Ruled a data race, consequence undefined behavior; 30 correct rounds are merely a sample. This article's opening infinite loop is another ending of the same affair: once the standard stopped governing, the compiler reasoned from "nobody else writes stop" straight into spinning in place.

### Condition Variables: The Bridge Is Borrowed from the Lock

In [*Waiting for a Condition: From Polling to Condition Variables*](../chapter03-wait-and-coordinate/01-wait-for-a-condition)'s minimal program, the worker changes `done` inside the lock, unlocks, and then calls `notify_one()`. The waiter enters `wait` holding the lock, with a predicate. The earlier claim that "on waking, `done` must be true" we now complete with the chain. The worker's `done = true` comes first, the scope-closing unlock after — sequenced-before. When the waiter wakes from `wait`, `wait` internally re-acquired the lock for it ([thread.condition.condvar] paragraph 8). Between that re-acquired lock and the worker's unlock, the standard writes synchronizes-with ([thread.mutex.requirements.mutex]/24). The reads after the predicate passes are sequenced-after the re-acquisition. Chain complete: `done` and the data beside it are fully visible to the waiter. Note — the visibility bridge never walked through the notify; it walked through the lock.

Put the notify back into the chain. The clause's entire statement of notify's effect is one sentence: unblock one waiting thread ([thread.condition.condvar]/5). Synchronization — not one word. The notify's value is temporal: it pulls the sleeping thread back to walk the "take lock, check predicate" road again, and the visibility comes from the re-acquire step along that road. 3.1's "predicate and lock must be used as a pair; drop either one and things break" reads even clearer in chain language: the bridge lives in the lock; the notify only calls people back to the bridgehead. Lost wakeups are the same sentence: with no waiting `wait` to unblock, the notify has no one to bridge with, and it keeps no record of its own. In *Waiting for a Condition*'s experiment, the notify went unanswered for 150 ms while `worker` slept on in `wait`. Count it through and accept: the bridging has always been done by the predicate and the same lock.

### cppreference's Thread Safety Paragraphs: You Can Now Read Them Yourself

With that said, let's return to reading documentation. The Thread safety paragraphs at the bottom of cppreference's pages use exactly this language. The `std::atomic<std::shared_ptr>` page has one (original text checked 2026-11-16):

> If multiple threads of execution access the same std::shared_ptr object without synchronization and any of those accesses uses a non-const member function of shared_ptr then a data race will occur unless all such access is performed through an instance of `std::atomic<std::shared_ptr>` …

Sentence by sentence into chain language. Without synchronization: no happens-before between the two threads' accesses. Data race will occur: the ruling of [intro.races] paragraph 17 holds. Performed through an instance of `std::atomic<std::shared_ptr>`: move the accesses onto atomic operations, and the bridge is built by them. Other pages' Thread safety paragraphs are variants of the same three-piece wording — read along the same lines.

### What to Ask When Checking

Given concurrent code, my habit is to walk the shared reads and writes in this order:

1. Between the reads and writes under check, is there a bridge? Only countable paired synchronization actions count (unlock with lock, construction with thread function, thread termination with join, release with acquire).
2. Do the bridge's two ends connect to the reads and writes under check? The write must precede the unlock (or release store), the read must follow the lock (or acquire load), and the chain must not break mid-way.
3. Where no bridge can be counted, who supplies it? Adding a lock, adding a join, moving accesses onto atomics — all are bridge-building roads. Supply nothing, and the program exits the standard's jurisdiction; however pretty the readings, they are only samples.

## Next Steps

Order, felt through two layers: the hardware layer's coherence guarantees "writes are eventually visible", and the language layer's happens-before decides when visibility arrives and which of two steps precedes which.

Before turning the page, compile and run this article's litmus — both commands are above. With energy to spare, delete the two `pin` lines and run again: with the scheduler swapping the two threads across cores, measure yourself how the readings move.

## Reference Resources

The progression borrows from Anthony Williams's *C++ Concurrency in Action*, taking its memory-model chapter. The plain-words treatment of caches and coherence references Paul McKenney's book, published in full online. The happens-before writing references the experimental articles on Preshing on Programming. What was borrowed is structure and exposition; the examples and prose are my own.

- [Multi-threaded executions and data races (cppreference)][cppref-multithread], the overview page for data races and happens-before.
- [Current standard draft: Data races (intro.races)][draft-intro-races], the definitions of conflict, the synchronization relationship, happens-before and strongly happens before, and visible side effects.
- [Current standard draft: Sequential execution (intro.execution)][draft-intro-exec], the definition of sequenced-before.
- [Current standard draft: Abstract machine (intro.abstract)][draft-intro-abstract], the origin of the as-if rule.
- [Current standard draft: Mutex requirements (thread.mutex.requirements.mutex)][draft-mutex], the lock total order and the unlock/lock synchronization relationship.
- [Current standard draft: thread constructors (thread.thread.constr)][draft-thread-constr], the synchronization between construction completing and the new thread function's start.
- [Current standard draft: thread members (thread.thread.member)][draft-thread-member], join's synchronization relationship.
- [Current standard draft: Class condition_variable (thread.condition.condvar)][draft-condvar], wait's staged actions and notify's effect.
- [Current standard draft: Order and consistency (atomics.order)][draft-atomics-order], the meaning of memory orders, the release/acquire synchronization relationship, and the seq_cst total order.
- [Current standard draft: Class template barrier (thread.barrier.class)][draft-barrier], the completion step and the strongly-happens-before relations, cited when revisiting 3.7.
- [std::atomic's shared_ptr specialization (cppreference)][cppref-atomic-shared-ptr], the page holding the Thread safety paragraph quoted here.
- [Memory Reordering Caught in the Act (Preshing on Programming)][preshing-reordering], an experiment of the same shape as this article's litmus, the reference for measuring store-buffer pass-through.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams], the memory-model chapter.
- [Is Parallel Programming Hard, And, If So, What Can You Do About It? (Paul E. McKenney)][mckenney], the cache chapters.

[cppref-multithread]: https://en.cppreference.com/w/cpp/language/multithread
[draft-intro-races]: https://eel.is/c++draft/intro.races
[draft-intro-exec]: https://eel.is/c++draft/intro.execution
[draft-intro-abstract]: https://eel.is/c++draft/intro.abstract
[draft-mutex]: https://eel.is/c++draft/thread.mutex.requirements.mutex
[draft-thread-constr]: https://eel.is/c++draft/thread.thread.constr
[draft-thread-member]: https://eel.is/c++draft/thread.thread.member
[draft-condvar]: https://eel.is/c++draft/thread.condition.condvar
[draft-atomics-order]: https://eel.is/c++draft/atomics.order
[draft-barrier]: https://eel.is/c++draft/thread.barrier.class
[cppref-atomic-shared-ptr]: https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic2
[preshing-reordering]: https://preshing.com/20120515/memory-reordering-caught-in-the-act/
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
[mckenney]: https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook-e1.html
