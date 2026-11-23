---
title: "A Practice Ground for the Criteria: Stop Flags, Reference Counting, and DCLP"
description: "The memory-order criteria back in real scenes: stop flags set with release and read with acquire, reference counts incremented relaxed and decremented acq_rel, double-checked locking and the Meyers singleton, plus publish-subscribe, fence and volatile boundaries, with the tier-choosing criteria laid out together."
chapter: 5
order: 5
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
translation:
  source: documents/vol5-concurrency/chapter05-atomic-memory-model/05-memory-order-patterns.md
  source_hash: a26e55f86f7ae0bcdcc4e8b5bdd23c43f4b2756e6032d7c16f9eeee12ab1a567
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 20917
---

# A Practice Ground for the Criteria: Stop Flags, Reference Counting, and DCLP

The previous article finished counting the four tiers' temperaments; this one puts them back into real scenes: stop flags, reference counting, double-checked locking — each spot picking its tier live, by the criteria.

## Common Patterns: The Criteria's Practice Ground

The three criterion questions, applied one by one: who are the bridge's two ends, what goods cross the bridge, and where on the chain does the operation stand.

### Stop Flags: release Set, acquire Read

Before 1.6 covered cooperative stopping, the polled flag was the version everyone had written: the worker repeatedly checks the flag, the main thread sets it, and most of the time some configuration is updated alongside. 5.3 counted the relaxed version's bridge as not one section built; we write the fix as a demo, leaving the relaxed control to the hands-on exercise at the end:

<OnlineCompilerDemo
  title="Stop Flag: release Set, acquire Read, the Configuration Crosses Too"
  source-path="code/examples/vol5/chapter05/15_stop_flag.cpp"
  description="main changes batch_size, then sets the flag with release; worker sees the stop with acquire and reads the configuration, getting 64 rather than the stale 8."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// stop_flag.cpp —— 停止标志：release 置、acquire 读，标志旁边的数据跟着过桥
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

std::atomic<bool> stop_flag{false};
int batch_size = 8;              // 标志旁边共享的配置，普通变量

int config_after_stop = 0;

void worker() {
  while (!stop_flag.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  config_after_stop = batch_size;   // 看到停止之后读配置
}

int main() {
  std::thread w(worker);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  batch_size = 64;                  // 配置更新压在置标志之前
  stop_flag.store(true, std::memory_order_release);
  w.join();
  std::printf("worker 看到停止后读到的 batch_size=%d\n", config_after_stop);
}
```

</OnlineCompilerDemo>

```text
worker 看到停止后读到的 batch_size=64
```

Count the bridge section by section. The write to batch_size sequenced-before the release store on stop_flag — program order. The worker's acquire load takes true, and synchronizes-with connects. The configuration's read comes after the acquire. Three sections joined: 64 comes through, and 8 never appears again.

When is relaxed truly enough? When nothing shared sits beside the flag. The stop signal is the entire content: seen means acted, with no goods needing to cross — relaxed's whole action suffices. The judgment is yours: after reading the flag, who else will you read? Not one name you can count, and relaxed is safe to use. Names you can count, and the writing side gets release, the reading side acquire. On this machine, an acquire load compiles to the same mov as a relaxed one — adding the bridge adds no instruction. On weakly ordered machines, acquire pays the fence price, and that is the price of correctness.

### Reference Counting: relaxed Increment, acq_rel Decrement

Reference counting is the model scene of relaxed and acq_rel in one frame. The complete intrusive pointer lives in the old ch03 source article *Atomic Operation Patterns*; here we keep only the two member functions, with the memory orders as the protagonists:

<OnlineCompilerDemo
  title="Reference Counting: relaxed for add, acq_rel for release"
  source-path="code/examples/vol5/chapter05/16_refcount_core.cpp"
  description="The two in-text member functions live verbatim in Counted, with a runnable main added: refs climbs 1 to 3, hands release one by one, and the release_ref that reaches zero returns true."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// refcount_core.cpp —— 引用计数两个成员的内存序搭配
// （正文只留两个成员函数，这里补一个 main 展示行为；完整的侵入式指针见素材文）
#include <atomic>
#include <cstdio>

struct Counted {
  std::atomic<int> refs{1};

  void add_ref() {
    refs.fetch_add(1, std::memory_order_relaxed);   // 只要动作完整
  }

  bool release_ref() {
    // 减到零的线程要销毁对象。读的一头接住别的线程的使用（acquire），
    // 写的一头把自己的使用压在减法之前（release）。
    return refs.fetch_sub(1, std::memory_order_acq_rel) == 1;
  }
};

int main() {
  Counted c;                        // refs 从 1 起步
  c.add_ref();                      // 拿一个新引用：1 -> 2
  c.add_ref();                      // 再拿一个：2 -> 3
  std::printf("两次 add_ref 后 refs=%d\n", c.refs.load());

  while (!c.release_ref()) {}       // 逐个放手，减到零的那一次返回 true，销毁权归它
  std::printf("减到零的那一次拿到销毁权，refs=%d\n", c.refs.load());
  return 0;
}
```

</OnlineCompilerDemo>

The criteria, counted at both ends. The decrement end: whoever reaches zero destroys the object, and destruction must see every other thread's uses of it — so the decrement's read must be acquire, catching the upstream. Meanwhile everyone who used the object must press their own accesses before their decrement — so the decrement's write must be release, publishing downstream. Both ends chained, acq_rel. The increment end: whoever adds a reference cares only that their taking of the reference stands — a whole action suffices, relaxed. The increment and decrement get different treatment; that summary is our own, and the two member functions above are its landing spot.

### Double-Checked Locking: Correct Only Since C++11

Suppose you hold an object whose initialization is expensive — configuration reads files, connections shake hands — while most callers just want the finished thing. Locking on every access is correct, but the lock's cost lands entirely on the fast path, with 99% of calls paying for the 1% that initializes. Check, lock, check again, pushing the lock later — that is the shape of the **double-checked locking pattern** (DCLP). Before C++11 it could not be implemented correctly; Scott Meyers and Andrei Alexandrescu's 2004 analysis wrote out the old standard's difficulties in full, and C++11's memory model laid its foundation. The usable spelling:

<OnlineCompilerDemo
  title="Double-Checked Locking: acquire Outside, relaxed Inside the Lock, release Publishing"
  source-path="code/examples/vol5/chapter05/17_dclp.cpp"
  description="Eight threads each take the instance 100000 times; pointers agree, fields complete. The crash scene from the missing pointer-return line is below, in AddressSanitizer's reply."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// dclp.cpp —— 双重检查锁定：外层 acquire，锁内 relaxed，发布 release
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

struct Config { int id; int value; };
Config* raw_instance = nullptr;              // 真正的对象
std::atomic<Config*> published{nullptr};     // 发布口
std::mutex init_mutex;

__attribute__((noinline)) Config* instance() {
  Config* p = published.load(std::memory_order_acquire);        // 外层检查，acquire
  if (p == nullptr) {
    std::lock_guard<std::mutex> lock(init_mutex);
    p = published.load(std::memory_order_relaxed);              // 锁内复查，已持锁，relaxed 就够
    if (p == nullptr) {
      raw_instance = new Config{1, 42};                         // 构造全部完成才发布
      published.store(raw_instance, std::memory_order_release); // 桥从这里搭
      p = raw_instance;      // 发布后把指针带回来；第一版漏了这行，抢到锁的线程拿着 nullptr 回去
    }
  }
  return p;
}

const Config* seen[8] = {};

int main() {
  std::thread ts[8];
  for (int i = 0; i < 8; ++i) {
    ts[i] = std::thread([i] {                  // seen 是全局数组，lambda 里直接可见，不用捕获
      for (int k = 0; k < 100000; ++k) {
        const Config* p = instance();
        if (p->id != 1 || p->value != 42) {
          std::printf("线程 %d 读到不一致的对象\n", i);
        }
        seen[i] = p;
      }
    });
  }
  for (auto& t : ts) t.join();
  bool all_same = true;
  for (int i = 1; i < 8; ++i) if (seen[i] != seen[0]) all_same = false;
  std::printf("8 条线程各取 10 万次：指针一致=%d，id=%d value=%d\n",
              all_same ? 1 : 0, seen[0]->id, seen[0]->value);
}
```

</OnlineCompilerDemo>

```text
8 条线程各取 10 万次：指针一致=1，id=1 value=42
```

I ran the reading twice; identical both times. The memory-order division of labor, laid out: the outer check uses acquire — reading non-null must guarantee the completed construction is visible, or reading the fields afterwards means reading a half-constructed object. The in-lock recheck uses relaxed — taking the lock has already built the bridge for it (the lock's synchronization relationship counted in 5.3), so relaxed needs only a whole read. The publication uses release, with all of construction's writes pressed before it. The three questions, each in place.

The reading didn't come easily; I leave the crash scene to you. At the moment of the crash, I stared at the segfault for a good while. The first demo version was missing one line: inside the lock, after constructing and publishing, it forgot to carry the pointer back into the local — the lock-winning thread returned holding nullptr, and the program died on the spot. AddressSanitizer's report pointed out the location in two lines:

```text
==43==ERROR: AddressSanitizer: SEGV on unknown address 0x000000000000
    #0 ... in operator() /tmp/concurrency-u5/dclp.cpp:33
```

Null-pointer dereference; line 33 is exactly where the thread function reads the fields. The machine caught it for me; adding the line `p = raw_instance` brought the readings home.

I tell the story because it measures the boundary between two kinds of errors precisely. Forgetting to carry the pointer back, the local machine catches — the readings explode on the spot. Memory-order errors the local machine cannot catch: swap every acquire and release in the implementation for relaxed, and on x86 it will most likely stay correct, with TSO carrying the write/read order for you. The warrant for correctness is not the readings but the standard semantics: release publication makes the completed construction visible to the acquire read; remove the bridge, and with no happens-before between construction and use, [intro.races] paragraph 17 rules it a data race, undefined behavior. Only on weakly ordered machines will the relaxed version truly read a half-constructed object. I haven't tested those machines, this article's readings don't cover them, and I keep that sentence on my own record.

The Meyers singleton waits nearby to close. Block-scope static initialization has its concurrency managed by the standard ([stmt.dcl] paragraph 3: on concurrent entry to the declaration, concurrent execution waits for initialization to complete):

<OnlineCompilerDemo
  title="Block-Scope static: Concurrent Initialization Guaranteed by the Standard"
  source-path="code/examples/vol5/chapter05/18_meyers_singleton.cpp"
  description="Two calls to config() obtain the same object, fields complete. If it can be written as a block-scope static, hand-rolled DCLP is unnecessary; Config reuses dclp.cpp's definition."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// meyers_singleton.cpp —— 块级 static 的并发初始化由标准担保
// （正文片段的补全版：Config 沿用 dclp.cpp 的定义，main 演示取到的是同一个对象）
#include <cstdio>

struct Config { int id; int value; };

Config& config() {
  static Config inst{1, 42};   // 并发初始化由标准担保
  return inst;
}

int main() {
  Config& a = config();
  Config& b = config();
  std::printf("两次取到同一个对象=%d，id=%d value=%d\n",
              &a == &b ? 1 : 0, a.id, a.value);
  return 0;
}
```

</OnlineCompilerDemo>

Whatever you can write as a block-scope static, don't hand-roll DCLP for. DCLP's value lies in the shape itself — check, lock, check again — applicable to lazily initializing any resource; the singleton is merely its most famous employer.

## Publish–Subscribe Flags: relaxed Counting with acquire-release (Extension)

This section carries the (extension) mark; skipping it does not affect the main line. Wanting to see progress is fine — but at the visibility level, a completion flag must carry it: you acquire true, and only then are the goods truly in hand. The division of labor, laid out, is another scene of relaxed and acquire-release in one frame:

<OnlineCompilerDemo
  title="Publish–Subscribe: A relaxed Count Reports Progress; Visibility Belongs to the Flag"
  source-path="code/examples/vol5/chapter05/19_pubsub.cpp"
  description="done's release and acquire carry three ordinary goods across the bridge; the consumer sees count 3 and goods 100, 101, 102. After the bridge connects, even relaxed suffices for reading progress."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// pubsub.cpp —— 发布订阅：relaxed 计数只报进度，acquire/release 标志承担可见性
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic<int> finished{0};    // 进度计数，relaxed
std::atomic<bool> done{false};   // 完成标志，release/acquire
int goods[3] = {0, 0, 0};        // 生产的三份货，普通变量

void producer() {
  for (int i = 0; i < 3; ++i) {
    goods[i] = 100 + i;
    finished.fetch_add(1, std::memory_order_relaxed);   // 只报进度
  }
  done.store(true, std::memory_order_release);          // 桥在这里搭
}

int seen_count = -1;
int seen_goods[3] = {0, 0, 0};

void consumer() {
  while (!done.load(std::memory_order_acquire)) {}      // 读到 release 写的值，桥接通
  seen_count = finished.load(std::memory_order_relaxed); // 桥接通后，读进度连 relaxed 都够
  for (int i = 0; i < 3; ++i) seen_goods[i] = goods[i];
}

int main() {
  std::thread p(producer);
  std::thread c(consumer);
  p.join();
  c.join();
  std::printf("消费者看到的计数=%d，货物=%d,%d,%d\n",
              seen_count, seen_goods[0], seen_goods[1], seen_goods[2]);
}
```

</OnlineCompilerDemo>

```text
消费者看到的计数=3，货物=100,101,102
```

Walk the division of labor through the criteria. finished's three increments use relaxed: a reader missing an update or two does no harm, the number being mere progress. done's two ends use release and acquire: the three goods are ordinary variables, and without the bridge they cannot reach the far side. Once the bridge connects there is one more free corollary, singled out here. The consumer's later read of finished needs not even acquire — relaxed is enough. The warrant is the bridge's shape: every write before the writing side's release is visible to every read after the reading side's acquire. cppreference's release-and-acquire section says exactly that, in those words.

The miswritten shape I also lay beside for contrast: a single relaxed flag reporting completion. Run it locally and it will most likely stay correct too, TSO helping. In standard semantics there is no bridge, and the goods may not all be present when the far side starts work. This kind of error only shows its face on weakly ordered machines — again that sentence: this article's readings don't cover them.

## The Boundaries of fence and volatile

Up to here, ordering has been bound to specific atomic operations — release grows on the store, acquire on the load. A fence (a barrier making the compiler and CPU halt at one position, with memory operations before and after barred from crossing) takes the ordering off the operation and places it at a position in the code by itself. `std::atomic_thread_fence` takes a memory-order parameter, since C++11. This section is elective; we take only the parts the criteria need, and skipping it does not affect the main line. For provenance: the fence-atomic synchronization and volatile discrimination pieces carry over from the old ch03 source article *fence and Compiler Barriers*; the remaining depth belongs to the electives.

We cover only the hookup with fences on both ends. cppreference's `atomic_thread_fence` page lists three hookups — fence-to-atomic, atomic-to-fence, fence-to-fence — and the demo uses fence-to-fence. The conditions in full: the writing side orders a release fence before an atomic store, whose memory order can be anything. The reading side takes the value with a relaxed load, then orders an acquire fence after the load. Both ends gathered, the bridge connects: writes before the writing side's fence are visible to reads after the reading side's fence.

<OnlineCompilerDemo
  title="fence to fence: Ordering Constraints Moved from the Atomics onto the Fences"
  source-path="code/examples/vol5/chapter05/20_fence_demo.cpp"
  description="The publishing side hangs a release fence, the subscribing side an acquire fence, both flag accesses demoted to relaxed — and the consumer still reads payload=42."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// fence_demo.cpp —— fence-fence 同步：两端各挂一条 fence，排序约束从原子操作挪到 fence 上
#include <atomic>
#include <cstdio>
#include <thread>

int payload = 0;                 // 普通变量，可见性靠 fence 与 flag 共同垫起来
std::atomic<int> flag{0};

__attribute__((noinline)) void publish() {
  payload = 42;
  std::atomic_thread_fence(std::memory_order_release);   // 独立 fence 承担排序
  flag.store(1, std::memory_order_relaxed);              // relaxed store 就够
}

int received = 0;

__attribute__((noinline)) void subscribe() {
  while (flag.load(std::memory_order_relaxed) != 1) {}
  std::atomic_thread_fence(std::memory_order_acquire);
  received = payload;                                     // 依据标准语义，此处读到 42
}

int main() {
  std::thread p(publish);
  std::thread c(subscribe);
  p.join();
  c.join();
  std::printf("消费者读到的 payload=%d\n", received);
}
```

</OnlineCompilerDemo>

```text
消费者读到的 payload=42
```

The instruction shape is isomorphic to release paired with acquire; the ordering money moved from the store and load ends onto the two fences, and the flag's accesses both dropped to relaxed. The fence's own instructions we also captured, compiled by the local GCC 16:

```text
fence_release:  ret                      ← 函数体只剩返回，零指令
fence_acquire:  ret                      ← 同上
fence_seqcst:   lock orq $0x0,(%rsp)     ← 一条带 LOCK 前缀的指令
```

cppreference's Notes state that on x86, `atomic_thread_fence` emits not one CPU instruction except for seq_cst, affecting only compile-time code motion — matching the instructions we captured. Textbooks drawing the seq_cst tier usually draw an mfence; the local GCC 16 actually compiles it to a LOCK-prefixed or.

On this machine no difference shows, and I say so plainly. The fence's value is not in saving instructions but in pushing the payment moment to after a runtime confirmation. cppreference's page has a mailbox-scan example using the other hookup, atomic-to-fence: 32 mailboxes read one by one with relaxed, and only on reading a slot belonging to you is the acquire fence inserted — non-matching slots pay no synchronization. Memory-ordered atomics cannot do this division: acquire grows on the read, and one read pays the bridge toll once.

I arrange the criterion like this: by default, write memory-ordered atomics — explicit, bridges easy to count. Reserve fences for scenes where the synchronization position must genuinely be separated from the operation; the price is that counting bridges turns winding — fence before or after the operation, and which write the load actually took — three conditions, miss one and it breaks, easy to overlook in review.

volatile's boundary was half-judged in 5.2; we gather it here. volatile promises exactly one thing: every access truly happens, merged or deleted by no compiler. Its day job is hardware register mapping and signal handling. It guarantees no whole actions, arranges no order, builds no bridge; used for cross-thread flags and counts, [intro.races] paragraph 17 rules it a data race. MSVC historically added acquire and release semantics to volatile — a non-standard extension; if code must cross compilers, don't lean on it. For cross-thread scenes you now hold a full toolkit: whole actions go to atomic, ordering to memory order, and volatile goes back to its registers and signal handlers.

## The Three fence Hookups and Instruction Details (Extension)

This section carries the (extension) mark; skipping it does not affect the main line, and the criteria do not lean on it. Skipping costs you the complete code of the other two hookups, a set of two-thread benchmark readings, and the details of what a fence corresponds to at the compiler and CPU levels. The main-text section unfolded only the fence-to-fence hookup; here we complete the three cppreference lists and drop one level further, to see what a fence compiles into. For provenance: the hookup code, benchmark readings, and platform-instruction explanations carry over from the old ch03 source article *fence and Compiler Barriers*; I copied the source article's benchmark source into the repository, and the citations below follow that copy.

### fence-atomic: The Writing Side Hangs a release fence, the Reading Side Keeps Its acquire Load

The conditions are counted with the same set as the main text; only the bridge's ends change dress: on the writing side, a release fence ordered before the atomic store, whose memory order can be anything — the source article demoted it to relaxed. On the reading side, a plain acquire load takes the writer's value and the bridge connects: the writing side's non-atomic writes and relaxed atomic stores before its fence are all visible to the reading side's subsequent reads. Set beside the main text's release store and acquire load, the difference is a single one: release's constraint is carried by a standalone fence before the store, and the store itself drops to relaxed:

```cpp
#include <atomic>
#include <string>

std::atomic<int> flag{0};
int payload = 0;

void producer()
{
    payload = 42;  // 非原子写入
    // release fence：保证上面的写入不会被重排到下面任何 store 之后
    std::atomic_thread_fence(std::memory_order_release);
    flag.store(1, std::memory_order_relaxed);  // relaxed store 即可
}

void consumer()
{
    // 等待 flag 变为 1，读端是 acquire 读，不需要独立的 fence
    while (flag.load(std::memory_order_acquire) != 1) {
        // spin
    }
    // 一定能看到 payload == 42
    int local = payload;
}
```

The source article's version of this passage originally wrote the reading side as a relaxed load followed by an acquire fence — the same shape as the main-text demo, counting as fence-to-fence. cppreference's condition for fence-atomic is that the reading side be an acquire operation, so I changed the reader back to an acquire load per the condition, and only then did the passage match its name. This passage and the mailbox scan below are illustrative snippets without runnable demos; the runnable, verifiable template is the main text's fence-to-fence spot.

### atomic-fence: The Mailbox Scan — the acquire fence Only on a Hit

This one flips the division: the writing side keeps its release store, and the reading side takes acquire off the load, inserting the fence only after a hit. The source article's scene is the mailbox scan, the very passage the main-text section foreshadowed: 32 mailboxes read one by one with relaxed, and only on reading a slot belonging to you is the acquire fence inserted — non-matching slots pay no bridge toll:

```cpp
#include <atomic>
#include <string>

constexpr int kNumMailboxes = 32;

std::atomic<int> mailbox_receiver[kNumMailboxes];
std::string mailbox_data[kNumMailboxes];

// 写入线程 i
void write_mailbox(int i, int receiver_id, const std::string& msg)
{
    mailbox_data[i] = msg;
    std::atomic_store_explicit(&mailbox_receiver[i],
                               receiver_id,
                               std::memory_order_release);
}

// 读取线程：扫描所有邮箱，只跟包含自己数据的邮箱同步
void read_my_mail(int my_id)
{
    for (int i = 0; i < kNumMailboxes; ++i) {
        if (std::atomic_load_explicit(&mailbox_receiver[i],
                                       std::memory_order_relaxed) == my_id) {
            // 只在匹配时插入 acquire fence
            std::atomic_thread_fence(std::memory_order_acquire);
            // 现在能安全地读取 mailbox_data[i]
            process(mailbox_data[i]);
        }
    }
}
```

Memory-ordered atomics cannot manage this division: acquire grows on the read, and each read pays the bridge toll once — one scan of 32 mailboxes pays 32 times. The fence version's non-matching slots pay only the relaxed read — a relaxed read still costs an atomic read and a cache access; what's saved is only the bridge-building part. How much is saved cannot be asserted from source code, and an acquire load need not actually generate a separate barrier instruction; comparing requires measurement, and the next subsection happens to have a set of readings.

### A Set of Benchmark Readings: fence plus relaxed versus Field-by-Field release/acquire

First, the benchmark harness. Each round the producer writes three ordinary data items, then publishes three atomic flags; the consumer polls until all three flags are up, takes the data, and returns an acknowledgment on another atomic; only on receiving the acknowledgment does the producer enter the next round. The two versions differ only in the publishing and reading memory orders: one uses field-by-field release stores with acquire loads; the other uses one pair of release and acquire fences with three relaxed stores and loads. The source is at code/examples/vol5/chapter05/fence_vs_ordered_benchmark.cpp, copied into the repository from the source article.

The readings come from an Intel Core i5-12400 (x86-64) under WSL2, built with CMake 3.20.5 and GCC 16.2, compile options -O3 -march=native, producer pinned to CPU 0 and consumer to CPU 2, 2 million rounds per test, 7 runs per spelling with the median taken:

| Spelling | Median of 7 |
| --- | ---: |
| Field-by-field release / acquire | 124.8 ns/round |
| fence + relaxed | 117.4 ns/round |

This round's fence-version median is slightly lower, and I state the boundaries in full. Single runs fluctuate noticeably; what's measured is the complete handover — polling, cache communication, and acknowledgment all included — and a single fence instruction's latency is only a small slice. On x86-64 both versions compile to the same machine instructions: release and acquire fences generate not one instruction, and release stores and acquire loads are plain movs — the main-text section's objdump readings addressed the same fact. The two medians' difference falls within run-to-run fluctuation and code layout, not reflecting any difference between the spellings themselves; these readings cannot prove the fence version is stably faster. To truly measure the difference, go to a platform where the generated instructions genuinely differ — ARM, for instance. To re-run locally, one line from the repository root:

```bash
g++ -std=c++20 -O3 -march=native -pthread \
  code/examples/vol5/chapter05/fence_vs_ordered_benchmark.cpp \
  -o /tmp/fence_vs_ordered_benchmark
/tmp/fence_vs_ordered_benchmark
```

Linux x86-64 required. The source writes the pinning as constants at the top — CPU 0 and CPU 2; on machines with fewer cores, change the two constants at the top before running.

### The Compiler Barrier: asm volatile and atomic_signal_fence's Lower-Level Counterpart

The first layer, at the instruction level, actually emits no instructions. The lowest-level compiler barrier in GCC and Clang is this line of inline assembly:

```cpp
asm volatile("" ::: "memory");
```

The assembly template is empty; no instruction is added to the machine code. The `volatile` guarantees the line itself isn't optimized away or merged with other statements. The trailing `"memory"` tells the compiler that this operation may read and write memory, forcing it to assume all memory accesses around it may be affected, and none may be reordered across this point. It guards against the optimizer's memory-moving hand; the CPU's out-of-order execution is beyond its reach.

This line is exactly what `std::atomic_signal_fence(std::memory_order_seq_cst)` looks like underneath on most platforms. `atomic_signal_fence` governs only ordering between ordinary code and a signal handler within the same thread: the handler runs on the same core as the interrupted thread, registers and caches are the same set, and at the CPU level there is no coherence problem to guard — the only thing to guard is the compiler moving memory accesses across the boundary, so it emits not one CPU instruction and is a pure compiler barrier. The boundary sits here too: inter-thread synchronization cannot count on the signal fence; on weakly ordered machines it guarantees no visibility, and for inter-thread work go back to `atomic_thread_fence`.

### CPU Barrier Instructions: x86's mfence, sfence, lfence and ARM's dmb, dsb, isb

The second layer is the CPU's own out-of-order execution, where barriers are architecture-specific instructions, and the two families look nothing alike.

x86/x86-64's memory model is called TSO (Total Store Ordering). Its constraints in plain words: hardware guarantees store-store and load-load ordering, and a store is never moved after a load that precedes it. The one reordering it permits is store-load — a store followed by a load, where the CPU may execute the load first. Under this model, acquire and release are nearly free, which is why the fence_release and fence_acquire captured in the main-text section compiled down to a lone ret. Only seq_cst truly needs an instruction, because what it must block is precisely the one permitted store-load. GCC usually doesn't emit mfence but `lock orq $0, (%rsp)` — an atomic OR of 0 against the stack top; the LOCK prefix is itself an omnidirectional barrier, faster than mfence on some microarchitectures and semantically equivalent. x86's three barrier instructions each have their portfolio: mfence omnidirectional, sfence on the store side, lfence on the load side. lfence plus sfence does not equal mfence — together they block load-load and store-store yet let store-load through, and that is exactly x86's one permitted reordering; an omnidirectional barrier cannot be assembled from them, and one must use mfence or a LOCK-prefixed instruction.

ARM is a weakly ordered architecture: most of the reorderings x86 blocks, it permits — store-store, load-load, store-load, load-store can all be reordered. Barriers on ARM are the daily bread of concurrent programming; writing multicore code cannot skirt them. Three barrier instructions are in common use. DMB (Data Memory Barrier) guarantees that memory accesses before it complete before accesses after it. DSB (Data Synchronization Barrier) is stronger — beyond ordering, it waits until memory accesses have truly reached their destinations. ISB (Instruction Synchronization Barrier) flushes the pipeline, needed after changing system registers, page-table switching for instance. DMB carries suffixes: ST covers only stores, LD only loads, and ISH denotes the inner shareable domain (the domain shared among cores — the most common). On ARM, a release fence usually compiles to DMB ISH, and an acquire fence to the lighter DMB ISHLD, which gates only the load direction. These two correspondences come from the source article's check against Arm documentation; I hold no ARM machine here and can give no measured readings — when you verify on such a machine, read the assembly first, then time.

These instructions aren't hand-written in daily work; the standard library's `atomic_thread_fence` and memory-ordered atomics wrap the choice. What to remember is that there is no fixed conversion table between memory orders and instructions — x86's seq_cst fence may bypass mfence, and on ARM, what ordered atomics and standalone fences each compile to likewise depends on architecture and compiler. Before comparing, read the assembly, then measure on the target platform.

## The Tier-Choosing Criteria, Laid Out Together

The title's question deserves a head-on answer. The three criterion questions, one full sentence each, to walk against your own code:

1. Between the reads and writes you're checking, can the bridge's two ends connect? The writing end lands on release (or unlock), the reading end on acquire (or lock), the reading end takes the value the writing end wrote — three sections joined into one, and acquire with release suffice, with no need for a more expensive tier.
2. Is the bridge guarding one handover, or the ordering of several mutually meshing atomic variables? One handover goes to acquire and release; mutual meshing goes to seq_cst. 5.3's litmus and 5.4's middle-tier readings draw the dividing line exactly here.
3. Where on the chain does this operation stand? Not chained, merely incrementing — relaxed. A read-modify-write chained at both ends — acq_rel. Too tangled to count — back to seq_cst.

We also measured the machine cost; the local x86-64, GCC 16.2.1, -O2 objdump readings arranged into a table:

| Action | relaxed | release / acquire | seq_cst |
|---|---|---|---|
| store | mov | mov | xchg |
| load | mov | mov | mov |
| read-modify-write | lock xadd | lock xadd | lock xadd |
| fence | no instruction | no instruction | lock orq |

The read-modify-write column includes acq_rel. On weakly ordered machines, release, acquire, and seq_cst pay the fence price; I hold no such machine and can give no readings.

Beneath the cost table lies one more asymmetry, laid in the open. When you cannot count the bridge and stay on seq_cst, you overspend a few instructions. When you count the bridge wrong, the price is correctness, and no clean reading can buy it back. The criteria lean conservative because the asymmetry of prices forces them to.

## Your Turn to Try

Before putting pen to code, I line up the questions banked across these two articles; walk them against your own concurrent code:

1. Between the reads and writes under check, count the bridge once and write its two ends in a comment. Writing end release, reading end acquire, the value written being read — miss one of the three sections and it must be mended.
2. Is the bridge guarding one handover, or several atomics' meshing? The former, acquire and release; the latter, stay on seq_cst.
3. Where does your read-modify-write chain at each end? Both ends chained is what earns acq_rel.
4. Is there data beside your counter that others will read? If not, relaxed can rest easy.
5. Does the codebase still hold `memory_order_consume`? Swap it for acquire.
6. Can the singleton be written as a block-scope static? If so, no hand-rolled DCLP is needed.

Two hands-on exercises. Exercise one: add a tier to the litmus — writing end stays release, reading end drops to relaxed. I pre-ran it twice, with hits of 109083 and 108575, nearly the same order as the middle tier. After running, think about why the reading barely moves: with the acquire end absent, the bridge was never built, and the machine paid nothing extra. An unchanged reading does not mean the bridge remains. Exercise two: change stop_flag's memory orders all to relaxed, run once — the reading is still 64 — then count the bridge, and judge for yourself whether to merge it.

## Next Steps

Looking back now at [*Same Counter, Two Threads — Why Does It Compute Wrong?*](../chapter02-sharing-data/01-from-wrong-programs)'s atomic_flag, 5.2's `atomic_flag` spinlock demo, and 5.3's litmus and synchronization table — every acquire and release in that code is something you can now count by the criteria.

6.1, *Hunting Races: Repetition Testing and TSan*, covers repetition testing and ThreadSanitizer; this article's recurring phrase "the local machine cannot tell" gets its tools there. Before turning the page, run the two tiers in the hands-on exercises and verify the readings yourself.

## Reference Resources

The experiments' readings and harness records, stated once. The tier added in the end-of-article exercise I pre-ran twice, readings recorded there. fence_demo's 42, stop_flag's 64, and pubsub's 3 with 100, 101, 102 read identically on every run. DCLP's two readings matched, with the crash scene located by AddressSanitizer. The instruction comparison comes from objdump, address column trimmed. The litmus harness is the same as 5.3's: release sentinel, monotonically increasing acknowledgments, reset pressed before the release, pinned to logical cores 0 and 8.

The eel.is draft used for assertion checking was fetched on 2026-11-16; the clause names are all carried in the text, and paragraph numbers drift with draft versions. [stmt.dcl] was checked for the waiting clause on block-scope static concurrent initialization. [intro.races]'s data-race adjudication was verified in 5.3 and reused here. The fence's three synchronization patterns, the x86 no-instruction Notes, and the mailbox-scan example were checked against cppreference's `atomic_thread_fence` page. The progression follows Anthony Williams's *C++ Concurrency in Action*, taking its unit arrangement and example-design thinking. The DCLP spelling referenced two materials: Jeff Preshing's article as one, and Meyers and Alexandrescu's 2004 analysis as the other. What was borrowed is structure and exposition; the examples and prose are my own.

- [std::memory_order (cppreference)][cppref-mo], the release-acquire section.
- [Current standard draft: Declaration statement (stmt.dcl)][draft-stmt-dcl], the waiting clause for block-scope static concurrent initialization.
- [Current standard draft: Data races (intro.races)][draft-intro-races], the data-race adjudication, verified in 5.3 and reused here.
- [std::atomic_thread_fence (cppreference)][cppref-fence], the three synchronization patterns, the x86 no-instruction Notes, and the mailbox-scan example.
- [Double-Checked Locking is Fixed In C++11 (Jeff Preshing)][preshing-dclp], the reference for the DCLP spelling.
- [C++ and the Perils of Double-Checked Locking (Scott Meyers, Andrei Alexandrescu)][meyers-dclp], the analysis of why DCLP failed under the old standard.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams], the reference for the progression.

[cppref-mo]: https://en.cppreference.com/w/cpp/atomic/memory_order
[draft-stmt-dcl]: https://eel.is/c++draft/stmt.dcl
[draft-intro-races]: https://eel.is/c++draft/intro.races
[cppref-fence]: https://en.cppreference.com/w/cpp/atomic/atomic_thread_fence
[preshing-dclp]: https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11
[meyers-dclp]: https://www.aristeia.com/Papers/DDJ_Jul_Aug_2004_revised.pdf
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
