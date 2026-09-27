---
title: "A World Without a Heap: How Memory Gets Handed Out, and What a Bitmap Is"
description: "Starting from the storage needs of tasks and messages, compare how FreeRTOS, ThreadX, and Zephyr handle memory allocation, then use the ZerOS source to sort out the division of labor among static reservation, the startup-time Arena, and the fixed-size block pool—and how a bitmap records block occupancy."
chapter: 1
order: 1
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 内存管理
  - 入门
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 15
prerequisites:
  - "Why an RTOS · Part 2: Project Bring-up, from an Empty Repository to the First Line of Renode Output"
related:
  - "Project Bring-up: From an Empty Repository to the First Line of Renode Output"
translation:
  source: documents/vol8-domains/embedded/zeros/01-heapless-memory/01-why-pool-and-bitmap.md
  source_hash: 52b88c326a27641437ca942edcda2b7882a093e100f247054faf4cc86910caa1
  translated_at: '2026-09-25T08:01:30+00:00'
  engine: anthropic
  token_count: 4800
---

# First Things First: Let's Get Memory Allocation Sorted

The good news is the board came up successfully, which at least tells us the project structure is sound. Next up: the code for our own kernel.

A natural question follows: scheduling, memory, synchronization—which one goes first? I thought it over briefly while writing and decided to start with memory. The reason is direct: the scheduler needs to register tasks, tasks need their own stacks, messages need somewhere to be parked; no matter which module we write later, we will have to answer the same question—where do these things live?

## Why We Need Memory Allocation

"That's odd," I'm sure someone will say, "memory? Isn't it just... there? The memory chip soldered onto the PCB?" Right. At the very, very beginning, everyone just declares one giant static array and works it in the crudest way possible: as you use it, bump the pointer forward; when it's given back, take it back. What sits behind this is exactly the slice of SRAM we carved out in the linker script. If you've forgotten where that configuration lives, open `third_party/ZerOS/src/board/stm32f103_bluepill/link.ld` and you'll see this line:

```ld
RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
```

These 20 KiB are the budget for the entire SRAM. Inside it we must fit global data, task stacks, and kernel state, and still leave room for the stacks used by exceptions and interrupts. What I want to stress is this: at runtime, the memory cost of everything we do adds up to just these 20 KiB.

An allocator cannot conjure memory out of thin air. What it can do fits in one sentence: carve a portion out of existing storage, hand it to some user, and record whom that portion belongs to now. If reclamation is allowed, it must also take it back once usage ends, so that later requests can reuse it.

Still a little abstract? Then let's take the task system we're about to write. A task needs at least two kinds of storage: **the task control block holds the state the scheduler cares about**

The task stack carries the task's runtime call context. After the task is switched out, those contents must stay in place; otherwise, the next time it switches back in, it won't even know where it had executed to.

Messages are the same: after a producer submits one, the consumer might not come to fetch it until a while later, and in the meantime it has to be kept somewhere.

This places demands on us. We have to see clearly how long a piece of storage must live and when it can be reused, and only then decide where it goes. Take a message buffer as an example: a buffer sits idle; a producer acquires its use, fills the message in, and hands it to the consumer; once the consumer finishes processing, the buffer is returned and can go on to hold the next message.

Here, "handing it to the consumer" is a transfer of usage rights; only after it is "returned" may that buffer be issued for the next message. We must keep this convention firmly in mind: the allocation interface, the queue interface, and the callers must all honor it together—otherwise, even if the allocator itself never misrecords state, two tasks could still end up modifying the same piece of memory at the same time.

### If It Can Be Arranged Up Front, Why Acquire at Runtime?

Aha, that's simple: we need to split into cases and discuss them by project scenario!

If the system always runs just a few fixed tasks after startup, and each task's stack size has already been worked out, then of course we can reserve a piece of storage for every task ahead of time. This is the situation we like best, because the scenario is completely clear—just reserve it and be done. If a message queue uses a fixed-capacity ring buffer, the whole buffer can be reserved in advance too. In other words, **having a memory need does not mean every one of our modules must call an allocator.**

But another class of needs knows only an upper bound, not who is using what at any given moment. Say we allow at most 8 pending messages at once, while exactly when messages arrive and when their processing finishes is decided by external events. We can prepare 8 slots in advance; at runtime, each incoming message claims one and returns it when done. The total amount of storage never grows, yet the usage rights keep changing—and that is exactly what calls for runtime allocation and reclamation.

There is also a very common middle case: tasks are created only during the startup phase, and once created they run for good. We don't want to hand-compute each task's address, **yet we also don't need to delete tasks or reclaim space at runtime.** Put bluntly: once allocated, it's off and running for good. A bump-forward allocation cursor handles this kind of need, and that is precisely what ZerOS's `Arena` later does.

At this point we can pull apart two questions that easily get tangled together: where memory comes from, and when it is decided who gets to use it. A statically reserved array can either serve a single task exclusively from beginning to end, or be handed to an allocator that doles it out to different users at runtime. When this series says "heapless", it means not relying on general-purpose heap allocation—it does not rule out the second usage.

### Why Not Just Wire Up a malloc

The convenience of a general-purpose heap is clear to all of us: callers can request space of different sizes by the byte, without fixing a quota in advance for every kind of object. To support allocation and reclamation, the allocator must record each block's size and state and search for a suitable free block; concrete implementations may also split large blocks and merge adjacent free blocks.

This approach can absolutely be implemented inside a fixed memory region on a microcontroller—there is no need for a desktop OS to exist first. What we really need to evaluate is the price it brings to the need at hand: how much space the metadata takes, whether mixed-size allocations produce external fragmentation, how many blocks a single request may have to search in the worst case, and how to synchronize when multiple tasks share it.

External fragmentation is easiest to see with a sketch. Suppose there are two free regions, 64 bytes each, with an object still in use sandwiched between them. The total free memory is 128 bytes, yet a request for 96 contiguous bytes cannot be satisfied directly. Merging adjacent free blocks alleviates this, but it cannot leap over an object still in use. Real-time behavior also depends on the concrete algorithm and configuration—we cannot conclude that the time cost has no upper bound merely because "it's called a heap".

With these questions in hand, let's look at what mature RTOSes already offer.

## How Established RTOSes Handle Memory Allocation

These schemes often appear together within one RTOS, and applications can combine them according to each object's purpose. We'll focus on three: FreeRTOS lays static creation and different heap implementations out on the table; ThreadX offers both a fixed-size block pool and a variable-size byte pool; and Zephyr helps us understand the difference between allocation algorithms and waiting strategies.

### FreeRTOS: The Caller Provides Storage, or the Kernel Requests It

Starting with task creation, FreeRTOS has two typical routes. `xTaskCreate()` has the kernel request the RAM needed for the task control block and stack; `xTaskCreateStatic()` instead has the caller provide the stack buffer and control-block storage. The latter puts the storage budget in the application's hands, while the act of creation can still happen at runtime—"static creation" does not mean the whole creation process completes at compile time. The [official notes on static versus dynamic allocation](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/03-Static-vs-Dynamic-memory-allocation) cover this distinction in detail.

Once we opt for dynamic allocation, FreeRTOS leaves the underlying implementation as a choice. The five official implementations each lean a different way:

| Implementation | How it allocates and frees                 | What to remember when choosing                        |
| -------------- | ------------------------------------------ | ----------------------------------------------------- |
| `heap_1`       | Allocate only, no free                     | Suited to objects created once and kept forever       |
| `heap_2`       | Supports free, does not merge adjacent free blocks | Consider fragmentation with repeated mixed-size allocations |
| `heap_3`       | Wraps the standard library's `malloc` / `free` | Depends on the standard-library allocator it plugs into |
| `heap_4`       | Supports free and merges adjacent free blocks | Mitigates external fragmentation, but cannot guarantee every request succeeds |
| `heap_5`       | Extends `heap_4` to manage multiple non-contiguous regions | Suited to systems whose RAM is scattered across several areas |

The trade-offs in this table are my distillation of the [official FreeRTOS memory management overview](https://docs.aws.amazon.com/freertos/latest/userguide/application-memory-management.html). Pay special attention to `heap_3` and `heap_5`: the former plugs into the standard library, the latter into multiple memory regions—so don't read all five implementations as "the same kind of allocation over the same static array".

What's worth borrowing here is asking first whether an object ever needs to be freed. If tasks are created only at startup and exist from then on, an allocate-only scheme has its place; if objects must be repeatedly created and deleted at runtime, then consider a scheme that supports reclamation.

> A heads-up before we go on: the ThreadX and Zephyr sections below come from my reading of the materials cross-checked with LLMs. I did not verify them line by line on real hardware, so they may be wrong—corrections welcome.

### ThreadX: Both Called Pools, but Block Pools and Byte Pools Serve Different Needs

Now ThreadX. Its `TX_BLOCK_POOL` provides fixed-size blocks: a request takes one block, a release returns the whole block. Free blocks are managed through a linked list, and both allocation and reclamation operate at the head of the free list. This suits resources of a fixed size that are repeatedly claimed and returned—for example, one kind of message node.

`TX_BYTE_POOL`, by contrast, lets you request by the number of bytes needed; internally it searches for a suitable free block, splits the remaining space, and merges adjacent free blocks during subsequent allocation searches. You can understand it as "a pool that feels closer to a heap". ThreadX also allows threads to wait when pool resources run short, so when we evaluate the API's return time, we must factor the waiting behavior in—details are in the Memory Block Pools and Memory Byte Pools chapters of the [ThreadX official documentation](https://github.com/eclipse-threadx/rtos-docs/blob/main/rtos-docs/threadx/chapter3.md).

So "we use a memory pool" is still not specific enough; we have to keep pressing: does the pool hand out fixed-size blocks, or byte regions of arbitrary length? When it runs dry, does it fail immediately, or suspend the current task? These two questions directly determine how the upper-layer code gets written.

### Zephyr: Fixed Blocks Fit One Need, and the Variable-Length Heap Has Its Own Timing Constraints

Finally, Zephyr. Its `k_mem_slab` is also a fixed-block model, managing free blocks with a free list. An application can create multiple slabs—for instance, keeping small messages and large buffers separate; when a slab runs empty, the caller can choose to wait for a block to become available. It avoids the external fragmentation that variable-size carving would cause inside a single slab, but the internal waste of a small object occupying a large block still exists—the [Zephyr Memory Slabs documentation](https://docs.zephyrproject.org/latest/kernel/memory_management/slabs.html) has the full story.

When we do need variable-length allocation, Zephyr also provides `k_heap`, whose underlying layer is `sys_heap`. The official documentation explicitly gives constant-time guarantees for `sys_heap` operations and limits the number of candidate blocks searched through compile-time configuration; the underlying layer itself does no concurrent synchronization—the upper `k_heap` is what adds synchronization and waiting. Note that constant time at the algorithm level is not the same as how long this waitable API takes in total—the time spent waiting for another thread to release is not part of the algorithm's guarantee; for those details, see the [Memory Heaps documentation](https://docs.zephyrproject.org/latest/kernel/memory_management/heap.html).

This is a reminder for us too: choosing a fixed-size block pool should be because it matches our objects' sizes, capacity, and lifetimes. ZerOS may choose a simpler implementation, but the justification has to land on our own requirements.

## How ZerOS Plans to Allocate

### Division of Labor by Lifetime

Back to our own code. The paths below are all relative to `third_party/ZerOS/`; you can open the files to look at the interfaces—no need to read the implementations in full for this article:

| Purpose                                    | Current source entry points                                                       | Storage arrangement                                                |
| ------------------------------------------ | --------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| Explicitly reserve task control blocks and stacks | `include/ZerOS/kernel/sched/task.hpp`, `stack.hpp`                          | `TaskSlot` keeps both pieces of storage together; the caller can define it statically |
| Create tasks per configuration at startup  | `include/ZerOS/kernel/sched/arena.hpp`, `src/arch/arm_cortex_m3/launch.cpp`      | `Arena` carves space forward per alignment requirements; no individual free |
| Fixed-size storage repeatedly acquired and returned | `include/ZerOS/kernel/mem/bitmap_allocate.hpp`                            | `BitmapPool` hands out whole blocks; freed blocks become available again |
| Construct and destroy objects inside pool blocks | `include/ZerOS/kernel/mem/typeable.hpp`                                      | `Make` / `Destroy` tie object lifetimes to claiming and returning blocks |

### For Objects That Need Reclaiming, We Hand Out One Block at a Time

Look at the pool's shape: it is fixed in its template parameters. For example, `BitmapPool<64, 8, false>` means 64 bytes per block, 8 blocks in total, with the poison-check policy off. The region users can actually put content in is `64 × 8 = 512` bytes; the whole pool object must also hold the bitmap, counters, and alignment padding, so don't take those 512 bytes as its total footprint.

At runtime we request one block and get 64 bytes of storage; when usage ends, the whole block goes back. There is no process of splitting a large free block into smaller pieces of different sizes, and no need to merge adjacent blocks on release. For this pool, as long as an empty block remains, a single-block request that fits the size requirement has somewhere to go. When a ninth block is requested while none of the first eight has been returned, `raw_allocate()` returns `OutOfMemory`.

Sizes must be worked out in advance. An 8-byte piece of data still occupies a whole 64-byte block—that is internal waste. Nor can we expect an 80-byte object to automatically splice two blocks together, because the current pool issues one block at a time. Different types may use the same pool at different times, as long as size and alignment both fit the requirements; you can also split into multiple pools by size or purpose. Sharing lets idle capacity be reused, while separate pools can keep a quota reserved for critical resources—the interface does not require the whole system to share one pool.

> Hey, hey! **When the quota runs out, the upper layer needs a plan.**
> A fixed-size block pool does not eliminate resource exhaustion. If ordinary messages and critical control messages share one pool, the former can take up every block. We need the upper layer to decide whether to drop this message, retry later, or reserve capacity separately for critical messages. As for our current `BitmapPool`, it has no wait queue and will not automatically suspend a task to wait for someone else to release.

Object construction we hand to yet another layer. Under our project's C++23 configuration, `Make<T>` first checks the object's size and alignment requirements, then claims raw storage from the pool, and finally constructs the object at the designated address with placement new. The standard non-allocating placement new uses the address the caller has already provided and does not go request another helping of space from the general-purpose heap; the corresponding `Destroy` first destroys the object, then returns the block. With this division, I believe "managing which block is available" and "how to construct this type" can each stay clear on its own.

### The Bitmap Answers One Question: Which Block Is Still Free

BitmapPool! We've finally gotten to the Bitmap—the bitmap!

Having decided to issue memory in whole blocks, we next need to record whether each block is occupied. A free list would work; so would a separate status table. The ThreadX block pool and Zephyr slab we saw earlier prove that the free list is a mature choice; ZerOS picks the bitmap here mainly because it wants the status data kept separate from the contents inside the blocks.

The scheme is intuitive: we record one bit per block—`0` means free, `1` means occupied. Below, blocks are drawn in ascending order by block number; note that this is not the usual binary notation with the high bit on the left:

```text
Block no.     0 1 2 3 4 5 6 7
Initial       0 0 0 0 0 0 0 0
alloc 1       1 0 0 0 0 0 0 0
alloc 1 more  1 1 0 0 0 0 0 0
free #0       0 1 0 0 0 0 0 0
alloc again   1 1 0 0 0 0 0 0
```

Our strategy is to scan from the lowest block number for the first `0`, so a released block 0 gets reused first. This makes the selection order easy to work through, but it provides no fairness between tasks, nor does it take turns using all blocks.

In the actual implementation, we organize the status into 32-bit words. When hunting for a free slot, we first skip full words, then find the first free bit within a word that isn't full yet. Judging whether a word is full means checking whether all its valid bits are `1`. A word being nonzero only tells you it contains occupied bits; it does not tell you it still has a free slot. If the last word does not carry a full 32 real blocks, the padding bits—those corresponding to no block at all—must be excluded too.

Now look at the layer of summarization `BitmapPool` adds on top of this: the second layer records one bit per block, and the first layer records, with one bit, whether a second-layer word is full. Find a group that isn't full, then find a free block within that group, and we no longer re-check every bottom-level word from scratch each time. This also explains why the next article writes out `base::Bitmap`, accessible both bit-wise and word-wise: the interfaces both layers need get filled in there.

Keeping block contents and the bitmap separate has one more benefit—we'll see it when we get to poison.

With the poison policy enabled, releasing fills the entire block with `0x67`, and the next time that block is chosen, we check whether the fill value has been tampered with—a way to catch illegal writes made after a partial release. The current code additionally uses `ever_poisoned_` to record which blocks have ever been filled with this value, so that initial storage isn't mistaken for corruption. This check reads and writes block contents and cannot catch every dangling-pointer problem, but because the status data occupies no bytes inside the block, checking the whole block stays easy to implement.

### "Predictable Time" Has to Get Concrete, Down to the Loops in the Code

Fixed capacity makes the worst-case path easy to analyze, but we cannot flat-out say "no matter how large the pool, allocation and release always take the same time". The current `Bitmap::find_first_zero()` searches word by word; the two-level pool searches the summary bitmap first, and when the summary is large enough, it still ends up checking multiple words. For `N` blocks, the first-level summary occupies `ceil(N / 1024)` 32-bit words, which gives an upper bound on the number of summary scans.

Let's plug in numbers: when `N` does not exceed 1024, the summary occupies a single word, and a lookup just locates the bit in the summary and then the bit in the corresponding group. As the pool keeps growing, this cost grows too. With the poison policy on, release must additionally fill the entire block, and re-allocating a previously freed block means checking its contents—work that scales with block size. The time to construct and destroy objects needs to be counted separately as well.

> **A non-blocking interface does not automatically mean interrupt-safe.** The current `BitmapPool` has no built-in lock, and bitmap modifications are not atomic operations. `try_allocate()` merely converts the allocation result into a pointer or a null pointer; **it will not conveniently resolve races for you—don't build into the primitive what plainly belongs at the composition layer**. When multiple tasks, or a task and an ISR, share one pool, we need to add critical sections or an exclusive-access convention ourselves, and count the critical-section time into the response time.

## Verifying One Claim-and-Return Cycle with Existing Code

We won't implement the allocator in this article, but we can take the repository's existing code and verify the model just described. Save the following program as `/tmp/zeros_pool_intro.cpp` and run it with a host compiler that supports C++23. The program has three steps: claim all 8 blocks, request a ninth to verify exhaustion, and finally release one block to verify reuse.

```cpp
// Platform: host; C++ Standard: C++23
#include "ZerOS/kernel/mem/bitmap_allocate.hpp"

#include <array>
#include <cassert>
#include <cstdio>

int main()
{
    ZerOS::memory::BitmapPool<64, 8, false> pool;
    std::array<void*, 8> blocks{};
    for (auto& block : blocks) {
        auto result = pool.raw_allocate();
        assert(result.has_value());
        block = *result;
    }
```

`BitmapPool<64, 8, false>` is exactly what we just described: 64 bytes per block, 8 blocks in total, poison off. The first eight `raw_allocate()` calls all succeed, and the pointers are stored in `blocks`. Next, the ninth request:

```cpp
    auto extra = pool.raw_allocate();
    assert(!extra.has_value());
    assert(extra.error() == ZerOS::memory::MemoryAllocationError::OutOfMemory);

    auto released = pool.raw_deallocate(blocks[0]);
    assert(released == ZerOS::memory::MemoryAllocationError::Ok);
    auto reused = pool.raw_allocate();
    assert(reused.has_value() && *reused == blocks[0]);
```

Look at the ninth request: what comes back is `OutOfMemory`. After returning block 0 with `raw_deallocate` and requesting again, we get the very same address back—the released block was reused. Finally, return all 8 blocks and print a one-line summary:

```cpp
    for (auto block : blocks) {
        auto result = pool.raw_deallocate(block);
        assert(result == ZerOS::memory::MemoryAllocationError::Ok);
    }
    std::puts("8 blocks allocated; ninth rejected; released block reused.");
}
```

From the tutorial repository root, we run it, with both the source and the executable kept under `/tmp/`:

```bash
# Heh, tucked away under /tmp so it stays out of our repo's development!
g++ -std=c++23 -Wall -Wextra -Werror \
    -I third_party/ZerOS/include \
    /tmp/zeros_pool_intro.cpp -o /tmp/zeros_pool_intro
/tmp/zeros_pool_intro
```

This run of mine was on host GCC 16.1.1, and the output was:

```text
8 blocks allocated; ninth rejected; released block reused.
```

What we verified here is capacity exhaustion and post-release reuse, not worst-case execution time on the board. In this small program the pool object is a local object, and its storage exists as long as it does; once it goes into the kernel, we must also arrange a long-enough lifetime for the pool itself. `BitmapPool` manages its own internal storage—it does not require that the storage come from a global variable.

Looking back, the needs we set out to solve are already quite concrete: long-lived fixed objects are reserved directly, startup-phase task assembly goes to `Arena`, fixed-size resources that must be repeatedly claimed and returned use a block pool, and the bitmap is in charge of recording which block is free. In the next article we write the bitmap out (a fixed-capacity Bitmap) and turn "find a free one" into testable code.

That's a wrap! Next, we get our hands on the more concrete design. (See you in the next article~)
