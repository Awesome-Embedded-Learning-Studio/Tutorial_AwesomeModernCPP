---
title: "SeqLock and Spinlocks"
description: "Two reader-centric atomic patterns: SeqLock uses sequence-number parity checking to keep readers completely lock-free, and the spinlock shows atomic_flag paired with acquire/release in minimal code, plus the applicable boundaries of each."
chapter: 5
order: 5
tags:
  - host
  - cpp-modern
  - advanced
  - atomic
difficulty: advanced
platform: host
cpp_standard: [11]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/cookbook/b-topics/05-seqlock-spinlock.md
  source_hash: 52743e4158b773f4b058d7908c18dc03c8967bc9fdefabc59d37de2e8696b9ac
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 9225
---
# SeqLock and Spinlocks

This piece is a split from the old volume's Atomic Operation Patterns. The source material packed seven patterns into one piece; the migration to the main text divided them up: the stop flag, reference counting, and double-checked locking went to Unit 5's [The Four Levels of Memory Ordering: From Sequential Consistency to relaxed](../../chapter05-atomic-memory-model/04-choosing-memory-orders), the publish-subscribe flag also lives there (extended section), lock-free maximum tracking no longer has a piece of its own, and the CAS-loop retry style was covered in [How Far Atomic Operations Take You](../../chapter05-atomic-memory-model/01-what-atomics-solve). The readings section keeps two items, SeqLock and the spinlock; the test for their being beyond the required line is direct: without them, Unit 5's main line (atomic operations, memory-order criteria) remains complete — what is left are purpose-built designs for two specialized scenarios, read-many-write-few and ultra-short critical sections, and we took them into the readings section.

Before reading it, you need the criteria of happens-before and acquire/release ([How One Thread Observes Another Thread's Modifications](../../chapter05-atomic-memory-model/03-happens-before) and [The Four Levels of Memory Ordering: From Sequential Consistency to relaxed](../../chapter05-atomic-memory-model/04-choosing-memory-orders)); the operation semantics of CAS and `atomic_flag` are in [How Far Atomic Operations Take You](../../chapter05-atomic-memory-model/01-what-atomics-solve).

The migration trail: mechanism explanations and code are preserved as they were; of the exercises only the SeqLock one migrated, plus one newly added spinlock timing exercise; what I changed is limited to numbering, frontmatter, and the cross-references pointing at the main text, plus a banned-words-list cleanup. The material claims no measured readings; the kernel behavior it touches (`seqlock_t`) cites public facts of the Linux kernel, with sources collected in the reference resources.

## SeqLock: Sequence Locking That Never Blocks Readers

### Pattern Motivation

One classic solution to the readers-writers problem is the reader-writer lock, but it comes at a high cost — even a pure read pays the full `lock_shared()` / `unlock_shared()` ritual, involving atomic operations and even system calls. In many scenarios the read frequency far exceeds the write frequency (sensor-data acquisition-and-reading, fetching the system time); we want reads to be as light as possible — ideally completely lock-free.

SeqLock was designed for exactly this. Its core idea on the table: a spinlock guards the writing side (only one writer at a time), but readers are never blocked; a reader checks a sequence number to judge whether the data it read is consistent. If the sequence number changed during the read (meaning a writer modified the data), the reader simply retries.

### Implementation

```cpp
#include <atomic>
#include <thread>
#include <iostream>

class SeqLock {
public:
    SeqLock() : sequence_(0) {}

    /// 写入者：获取写入权限
    void lock_write()
    {
        unsigned seq = sequence_.load(std::memory_order_relaxed);
        // 如果序列号是奇数，说明已经有写入者在工作
        if ((seq & 1u) != 0) {
            // 多写入者场景需要自旋等待或用额外的 mutex
            // 这里假设只有一个写入者
            return;
        }
        // 序列号加 1，变成奇数——标记"正在写入"
        sequence_.store(seq + 1, std::memory_order_release);
    }

    /// 写入者：释放写入权限
    void unlock_write()
    {
        unsigned seq = sequence_.load(std::memory_order_relaxed);
        // 序列号再加 1，变回偶数——标记"写入完成"
        sequence_.store(seq + 1, std::memory_order_release);
    }

    /// 读取者：在稳定状态下读取数据
    /// 返回读取开始时的序列号；调用者需要在读取后验证序列号是否变化
    unsigned read_begin() const
    {
        unsigned seq;
        for (;;) {
            seq = sequence_.load(std::memory_order_acquire);
            if ((seq & 1u) == 0) {
                // 偶数：没有写入者正在工作
                break;
            }
            // 奇数：有写入者正在工作，自旋等待
            // 实际实现中可以用 pause/yield 减少功耗
        }
        return seq;
    }

    /// 读取者：验证读取期间是否有写入发生
    /// 如果返回 true，说明读取是有效的
    bool read_validate(unsigned seq_before) const
    {
        unsigned seq_after = sequence_.load(std::memory_order_acquire);
        return (seq_after == seq_before) && ((seq_after & 1u) == 0);
    }

private:
    std::atomic<unsigned> sequence_;
};
```

Let us take apart the core mechanism of this design.

The parity of the sequence number is the crux; commit it to memory. Even means "no writer is working right now, the data is consistent"; odd means "a writer is modifying the data, it may currently be inconsistent". A writer flips the sequence number from even to odd at the start and back to even on completion — every successful write adds 2 to the sequence number.

The reader's strategy is "check before reading + validate after reading"; walk it through: read the sequence number, confirm it is even (no writer), then read the actual data, then read the sequence number once more. If the two sequence numbers match and both are even, no writer intervened during the read and the data is consistent. If they differ (or it turned odd), a write happened mid-read and the data may be inconsistent — the reader discards the result outright and retries.

The `memory_order_release` in `unlock_write()` and the `memory_order_acquire` in `read_begin()` / `read_validate()` establish the happens-before relationship: all of the writer's modifications to the actual data complete before `sequence_` returns to even — release guarantees those earlier writes are not reordered after the store. The reader sees the data only after `sequence_` has become even — acquire guarantees the later reads are not reordered before the load. The data a reader gets is therefore exactly the fully written version, safe for us to use.

### Usage Example

```cpp
struct SensorData {
    double temperature;
    double humidity;
    double pressure;
};

SensorData g_sensor_data;
SeqLock g_seq_lock;

// 写入者线程（通常是传感器采集线程）
void writer_thread()
{
    for (int i = 0; i < 100; ++i) {
        g_seq_lock.lock_write();

        g_sensor_data.temperature = 20.0 + i * 0.1;
        g_sensor_data.humidity = 50.0 + i * 0.2;
        g_sensor_data.pressure = 1013.25 + i * 0.01;

        g_seq_lock.unlock_write();
    }
}

// 读取者线程（可以有多个）
void reader_thread(int id)
{
    for (int i = 0; i < 100; ++i) {
        SensorData local;
        unsigned seq;

        do {
            seq = g_seq_lock.read_begin();
            local = g_sensor_data;  // 拷贝数据
        } while (!g_seq_lock.read_validate(seq));

        // 现在可以安全地使用 local——它是一个一致的快照
        std::cout << "Reader " << id << ": temp=" << local.temperature
                  << " humidity=" << local.humidity
                  << " pressure=" << local.pressure << "\n";
    }
}
```

Notice the reader copies the data into `local` and only then validates. This is a crucial detail: use it directly without copying, and by the time validation fails the data is already "dirty" — unusable and unrecoverable. A SeqLock reader must be ready to discard the read result at any moment, so the data read is either read-only (use and discard) or copied out before use.

### SeqLock's Applicable Boundaries

SeqLock has several limitations we must see clearly. It assumes at most one writer; with multiple writers, an outer mutex layer is mandatory. The data type read must be trivially copyable — if the data contains pointers or complex objects, hitting a partially modified state during the copy can cause undefined behavior. When writes are very frequent, readers may retry endlessly and performance falls below a reader-writer lock — SeqLock suits "few writes, many reads" scenarios. The Linux kernel's `seqlock_t` is the classic implementation of this pattern, used for time fetching (`do_gettimeofday`) and similar scenarios.

## The Spinlock: A Teaching Implementation and Where It Fits

### Basic Implementation

The spinlock is the simplest mutual-exclusion primitive — a thread that fails to acquire does not block but retries repeatedly in a tight loop. It is usually unfit for production (we will explain why shortly), but as a teaching tool it fits beautifully, because with the least code it displays the usage of `atomic_flag` and the basic principle of synchronization.

```cpp
#include <atomic>
#include <thread>
#include <iostream>

class SpinLock {
public:
    SpinLock() : locked_(false) {}

    void lock()
    {
        while (locked_.exchange(true, std::memory_order_acquire)) {
            // exchange 返回旧值：如果是 true，说明锁已经被占用，继续自旋
            // 如果是 false，说明我们成功获取了锁
        }
    }

    void unlock()
    {
        locked_.store(false, std::memory_order_release);
    }

private:
    std::atomic<bool> locked_;
};

int main()
{
    SpinLock spinlock;
    int counter = 0;

    auto work = [&](int times) {
        for (int i = 0; i < times; ++i) {
            spinlock.lock();
            ++counter;
            spinlock.unlock();
        }
    };

    std::thread t1(work, 1000000);
    std::thread t2(work, 1000000);

    t1.join();
    t2.join();

    std::cout << "counter = " << counter << "\n";  // 2000000
    return 0;
}
```

The `exchange(true, acquire)` in `lock()` is a clever operation: it atomically sets `locked_` to `true` while returning the previous value. If the old value is `false`, the lock was unoccupied and we acquired it. If the old value is `true`, someone else holds it and we keep looping. The `acquire` semantics guarantee that operations after acquiring the lock are not reordered before the `exchange`, and that the releasing thread's prior modifications become visible to the acquiring thread.

Now look at `unlock()`: its `release` semantics guarantee every write inside the critical section completes before the lock is released, so the next thread to acquire sees those modifications.

### Why Spinlocks Usually Do Not Fit Production

The spinlock's biggest problem is that it occupies the CPU while waiting. Consider the cases: if the critical section is very short (a few instructions), spinning may cost less than a mutex's context switch. But once the critical section grows a little longer, or several threads contend for the same lock, the spinlock wastes CPU time wholesale on "idle spinning". Worse, on a single-core system a spinlock is pure nonsense: the spinning thread hogs the CPU, the lock-holding thread never gets to run and release it, and deadlock arrives.

In real projects, prefer `std::mutex` or `std::shared_mutex`. Consider a spinlock only when several conditions hold together: the critical section is extremely short (no more than a few dozen instructions), contention is light, and you run on a multicore system. The Linux kernel uses spinlocks heavily in its preemptible-kernel paths, but the kernel has special scheduling guarantees (preemption disabled) that user space does not enjoy.

### A Better Version with atomic_flag

The `SpinLock` above is built on `std::atomic<bool>`, but the more canonical approach is `std::atomic_flag`; let us switch versions: it is the only atomic type the standard guarantees to be lock-free (`std::atomic<bool>` is theoretically allowed not to be):

```cpp
class SpinLockFlag {
public:
    SpinLockFlag() { flag_.clear(); }

    void lock()
    {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // test_and_set 原子地设置 flag 为 true 并返回旧值
        }
    }

    void unlock()
    {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};
```

`test_and_set` and `clear` are `atomic_flag`'s two core operations — the former atomically sets the flag to `true` and returns the old value; the latter atomically sets it to `false`. This version is semantically identical to the `atomic<bool>` version but guarantees lock-free. A version note while we are here: in C++11 the state of a default-constructed `atomic_flag` is unspecified and `ATOMIC_FLAG_INIT` is mandatory; the `clear()` in our constructor is belt and braces. From C++20 on, `ATOMIC_FLAG_INIT` is no longer required — the standard keeps it only for C compatibility, and default construction is equivalent to clear — so after moving to C++20 you can write `std::atomic_flag flag_{};` directly.

## When Do They Get Their Turn

With both patterns side by side, let us make the selection criteria concrete.

If the critical section is just a simple variable read or update (a counter, a flag, a maximum), the read-modify-write operations of `std::atomic` (`fetch_add`, CAS, and friends) suffice — no mutex, no spinlock. That is the lightest choice. For how to pick the memory order, we keep a full set of criteria in [The Four Levels of Memory Ordering: From Sequential Consistency to relaxed](../../chapter05-atomic-memory-model/04-choosing-memory-orders).

If the critical section involves coordinated modification of several variables (inserting into a map while updating a counter, say), `std::atomic` alone no longer suffices — unless you can pack the variables into one struct updated by CAS, honestly use `std::mutex`. The mutex pays for context switches, but it guarantees correctness and costs little under light contention; Linux's futex completes entirely in user space when uncontended.

If reads far outnumber writes and the data is trivially copyable, SeqLock is a fine choice — readers stay completely lock-free, paying only an occasional retry; the Linux kernel uses it in many high-frequency-read scenarios. The spinlock sits between mutex and atomic: it earns a spot only when the critical section is short enough that a mutex's context switch feels expensive, yet long enough that a single atomic instruction cannot wrap it. In everyday engineering we suggest measuring that lock contention truly is the bottleneck before considering it ([How to Adjust the Design](../../chapter06-validating-designs/05-adjusting-design)).

## Hands-On Exercises

Based on the `SeqLock` class above, write a complete program: one writer thread updates a struct with three `double` fields every 10 ms, while four reader threads each read and print the data every 1 ms. After running for a while, observe whether readers always obtain consistent data (all three fields from the same write). If inconsistency appears (say the temperature is from the 5th write but the humidity from the 6th), check whether your `read_begin` / `read_validate` are used correctly.

Then put `SpinLock` and `std::mutex` on the same exam paper: have 4 threads each increment one counter a million times, protected by the two locks respectively, recording total time and CPU usage. Next change the critical section from one increment to ten increments plus one `sleep_for(1ms)` and rerun — you will see with your own eyes how narrow the spinlock's window of advantage is.

> Some of the sample code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) under `code/volumn_codes/vol5/ch03-atomic-memory-model/`; for the rest, the code blocks in this text are authoritative — take and compare as you like.

## Reference Resources

- [Seqlock — Wikipedia](https://en.wikipedia.org/wiki/Seqlock)
- [Linux Kernel seqlock.h — source code](https://github.com/torvalds/linux/blob/master/include/linux/seqlock.h)
- [std::atomic_flag — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic_flag)
- [Preshing on Programming: Atomic vs. Non-Atomic Operations](https://preshing.com/20130618/atomic-vs-non-atomic-operations/)
