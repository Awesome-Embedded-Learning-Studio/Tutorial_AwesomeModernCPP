---
title: "Interrupts Only Send Notes: Ring Buffers, Worker Threads, and Bottom-Half Delay"
description: "We disassemble 3570f27(feat: message queue and worker thread): the fixed-capacity ring buffer has its capacity set to a power of two at compile time, the modulo operation for finding empty slots is replaced with a bitwise AND, a counting semaphore acts as a signal for data availability, and the worker thread is connected to the queue along with the task control block, without using a single heap memory. The ISR only sends and never sleeps, while the worker's heavy work can sleep for 200ms, which is the entire meaning of bottom-half delay. I ran seven host test cases in the /tmp copy, all 54 assertions passed, and I injected a character 'd' into USART1 in Renode, watching the worker report 'got the job', sleep for 200ms, and then report 'job done'. During its sleep, A prints once per second and B prints every 700ms, both printing as usual."
chapter: 5
order: 2
tags:
  - stm32f1
  - intermediate
  - embedded
  - circular buffer
  - callback mechanism
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "The Definition of a Second: Ticks, Wrap-around Arithmetic, and Expiry Queues"
  - "Who's the Most Urgent, Who Steps Up: Ready Bitmap, Task Three States, and Selection Arbitration"
  - "Faking a Birth Certificate at the Top of the Stack: PendSV, SVC, and the First Context Switch"
related:
  - "The Last Will of the Chip: HardFault Context, Stack Canary, and Zero-alloc Logging"
  - "Waiting 300ms Without Raising a Task: Software Timers and fn+void* Hard Currency"
current_status:
  title: Interrupts Learn to Send Notes
  detail: The ZerOS chapter is currently being polished
  variant: polishing
  icon: 📮
translation:
  source: documents/vol8-domains/embedded/zeros/05-context-economics/02-msg-queue-and-worker.md
  source_hash: af84a5dca807f6c0b3e3ca4eb3919568ae0cea73dc73e282df8b6db4bde9a270
  translated_at: '2026-11-22T13:45:28+00:00'
  engine: qwen3:14b
  token_count: 16038
---
# Interrupts Only Send Notes: Ring Buffers, Worker Threads, and Bottom-Half Delay

After wrapping up the previous station, the byte-receiving chain in the demo was a version we had verified as working: the USART1 interrupt receives a byte, stores it in a global `char`, and sends a semaphore to wake up the sleeping task. However, if you take another look, you'll notice an awkward fact hidden in the skeleton: the mailbox has only one slot. The byte is stored by the interrupt, and the retrieval is done by the task. Both contexts share the same `char`. If you type two keys in a row, the second byte will overwrite the first one that hasn't been retrieved yet, silently, without even giving an error.

`3570f27` ends this awkward situation. Its subject is "feat: message queue and worker thread", and in one go, it submitted 7 files, adding 365 lines. From this commit, the message queue and worker thread entered the kernel. The sister article "The Last Will of the Chip" deals with the other side of the context, explaining how the chip leaves a will in the HardFault context. We won't touch that content in this article, but we'll use its title to point the way. The only question we want to answer in this article is: **Which context is the code currently running in?** What can and cannot be done in the interrupt, and how can the things that cannot be done be passed on, and to whom? After disassembling the diff of `3570f27`, the answer is on the table.

## The Suffering of a Single Slot, Let's Count from the Beginning

The old chain that was replaced is worth a look, as we can find what it lost and what it left behind in the new code. The previous version of the demo had only three lines:

```cpp
constinit ZerOS::sync::Semaphore g_rx_sem{0};
constinit ZerOS::arch::cortex_m3::CortexM3CriticalSection mailbox_lock{};
char g_rx_byte = 0;
```

The semaphore's job is to wake people up, and the critical section protects the byte inside. The `char` itself is the entire mailbox. Let's count the task's posture: it `acquire`s the semaphore and sleeps, the byte comes, the interrupt puts in a unit, and it wakes up, locks the critical section, and copies the byte away. Have you noticed? The semaphore's count and the mailbox's slot are two separate things: the semaphore faithfully records how many bytes have arrived, but the mailbox only provides one slot. If two bytes arrive in quick succession, the count climbs to 2, and the task wakes up twice, but the two times it retrieves are the same slot's content. Let's go through the two times: the first retrieval gets the later byte, and the second retrieval still gets it, because the slot was already overwritten. The semaphore didn't make a mistake, it's the mailbox that can't hold it.

The new code merges both sides into the same thing. Let's look at the `3570f27` demo mailbox:

```cpp
// the RX mailbox: a real queue now — a burst of bytes survives intact,
// no slot sharing, no hand-rolled flags
constinit ZerOS::sync::Queue<char, 16> g_rx_queue{};
```

It has a total of 16 slots, each byte occupies its own slot, and no one can overwrite anyone else. The line in the comment, "a burst of bytes survives intact," is not just a slogan on the lips, the host side has a dedicated test case to check it, and we'll face it directly in the exam section. From now on, the count and the slot are two members of the same structure. The semaphore has become an organ of the queue. How it connects in, we'll follow the source code down.

## Fixed-Capacity Ring Buffer: Capacity is Set to a Power of Two at Compile Time

The mailbox has expanded from one slot to sixteen, and the problem of arranging the slots has followed. Following the naive array arrangement, every time a byte is taken from the head, the elements behind it are shifted forward one slot. The cost of taking one and shifting one is a normal expense on the serial port path where bytes are coming in continuously. The ring arrangement connects the beginning and end of the array, and the used slots are reused after a full rotation. The elements left in the slots remain untouched from head to tail, and the return to the beginning is achieved through simple arithmetic.

The queue in `3570f27` uses the ring arrangement. Its real home is in `include/ZerOS/kernel/sync/msg_queue.hpp`. Let's take a look at its skeleton:

```cpp
// messages cross the ring by memcpy
template <typename M>
concept PlainMessage = std::is_trivially_copyable_v<M>;

template <ZerOS::system::SystemContext System, PlainMessage Message, std::size_t Capacity>
struct QueueBase {
    static constexpr auto capacity = Capacity;
    constexpr QueueBase() = default;

    static_assert((Capacity & (Capacity - 1)) == 0, "power of two: & beats % on M3");

    Message ring_[Capacity]{};
    std::size_t head_{};
    std::size_t count_{};
};
```

Three members support the entire buffer: `ring_` is the fixed-capacity slot array, `head_` is the position we read from, and `count_` records how many elements are currently in the buffer. You'll find that it doesn't have a write pointer, because the write position is calculated, `head_ + count_` gives the next empty slot, and it's redundant to have a separate one for storing it. The `PlainMessage` concept requires the message type to be trivially copyable, and the comment brings the reason with it: messages cross the ring buffer through `memcpy`. Types with constructors, destructors, and heap pointers in the queue are copied as empty shells, so it closes the door at the type level.

The line with `static_assert` is worth taking a closer look at. `(Capacity & (Capacity - 1)) == 0` is the old method of checking for a power of two. After subtracting one from a power of two, all its lower bits are 1, and the result of a bitwise AND with itself is zero. For example, 6 minus 1 is 5 (binary 101), and the AND of 6 and 5 is 4, which immediately explodes the compilation. The comment brings in the motivation, which is the original phrase "power of two: & beats % on M3". In plain language, this means that the capacity is set to a power of two, and the bitwise AND beats the modulo operation on M3. The modulo operation on Cortex-M3 is a division instruction, while the bitwise AND is just an `and` instruction. Every cycle on the interrupt path is money, and making the capacity a power of two reduces the modulo operation to an AND operation, which is the cost of `Queue<char, 16>` but not `Queue<char, 15>`, which can't even compile. We think this trade-off is worth it, because fixed capacity is our design intent, and it's an exception to choose a capacity arbitrarily.

The write and read operations are short, let's look at them one by one:

```cpp
    QueueError post(const Message& m) {
        auto& sys = System::self();
        ZerOS::irq::CriticalGuard g{sys};
        if (count_ == capacity) {
            return QueueError::Full;
        }
        ring_[(head_ + count_) & (capacity - 1)] = m;
        ++count_;
        semaphore.release(); // the unit is handed out only AFTER the data landed
        return QueueError::Ok;
    }
```

`post` grabs the critical section right away, and we've seen this technique in previous stations. As long as a function is potentially called by an interrupt, shared state must be accessed with interrupts disabled. The check for being full is `count_ == capacity`, and if it's full, it returns `QueueError::Full` directly, not writing a single byte, and not moving any old data. This is called full rejection: the queue is full, and the new one is rejected, never silently overwriting. `Capacity` is set to a power of two by the compile-time assertion, so the lower bits of `capacity - 1` are all 1, and after a bitwise AND, `(head_ + count_)`老实ly wraps back to the beginning of the array, and modulo is downgraded to an `and` instruction. After writing, `count_` increases by one, and then it's `semaphore.release()`'s turn to appear. The comment's line "the unit is handed out only AFTER the data landed" explains the order: the unit is only handed out after the data has landed. If it were the other way around, the awakened person would grab the queue and find old data that hasn't been written in yet. We've had a loss from the order in the previous article on missed wakeups, and the warehouse hasn't had that loss again.

The read side is even shorter, let's see:

```cpp
    std::expected<Message, QueueError> pop_ring() noexcept {
        auto& system = System::self();
        ZerOS::irq::CriticalGuard g{system};
        Message out = ring_[head_];
        // pop one, we need to move our head!
        head_ = (head_ + 1) & (capacity - 1);
        --count_;
        return out; // explicit make
    }
```

`pop_ring` takes one out from `head_`, the read pointer also uses the bitwise AND to wrap back to the beginning, and `count_` decreases by one. Both actions are wrapped in a critical section, `post` and `pop_ring` operate on the same set of members. If the protection layer isn't there, the interleaving of `post` in the interrupt and `pop` in the task would eventually write dirty data. The return value uses `std::expected`, and we get either the message or the error code. The caller can check with `has_value()` and then dereference it if it's there.

## Counting Semaphore: A Signal for Data Availability, Letting Waiters Sleep Until the Unit Comes

The ring buffer itself doesn't wake people up. Bytes are pushed in, but how does the sleeping task know? The answer is the counting semaphore embedded in the queue. Let's look at its wiring:

```cpp
    std::expected<Message, QueueError> receive_one() noexcept {
        semaphore.acquire();
        return pop_ring();
    }
    // when 0, means try receive
    static constexpr auto TRY_RECEIVE_TIME = clock::Milliseconds{0};
    std::expected<Message, QueueError> receive_one(clock::Milliseconds timeout) noexcept {
        if (semaphore.try_acquire(timeout) != SyncError::Ok) {
            return std::unexpected(QueueError::Timeout);
        }
        return pop_ring();
    }
```

The logic of the counting semaphore, in plain language, is counting: it has a counter, `acquire` wants to subtract one from the counter, and if it can't (the counter is zero), it puts the current task into the waiting queue and sleeps. `release` adds one to the counter and also checks if anyone is waiting, waking up the head of the waiting queue if there is. It doesn't care what the data looks like, it only cares whether there is or not, so it's perfect for being a "data available" signal. When the queue is born, the initial value of the semaphore is zero, let's look at its member declaration:

```cpp
    SemaphoreBase<System> semaphore{0}; // zero units: the ring starts empty
```

The comment brings the semantics with it: the ring buffer is born empty, with no units, so no one can get anything right away. The two overloads of `receive_one` each handle a different posture, let's look at them one by one: the parameterless version `acquire`s and has no way back, if it can't get it, it sleeps forever, and only after getting it does it `pop_ring`, this is the posture of receiving mail in a mailbox. The version with a timeout uses `try_acquire`, and if it still can't get it by the time the timeout is reached, it returns `QueueError::Timeout`. And `TRY_RECEIVE_TIME` is set to zero milliseconds, so setting the timeout to zero is our way of asking for it: if it's there, take it, if not, leave immediately. All the `try` calls in the host test cases use this one.

The implementation of the semaphore lives in `semaphore.hpp`, and there's a comment in `release` that you should read as is:

```cpp
    void release() noexcept {
        auto& system = System::self();
        ZerOS::irq::CriticalGuard g{system};
        ++count_; // the unit ALWAYS lands in the counter — woken sleepers
                  // race for it (loser re-sleeps, that is POSIX-semantics)
        if (auto head = waiters_.pop_head()) {
            // Some one is waiting!
            system.ready(head->task_);
        }
    }
```

The place where the count lands is always the counter, waking up is just a side action, and the two actions are not bound: if the one who wakes up and another task compete for the unit and lose, the one who loses goes back to sleep, and the unit remains in the counter, no one is disadvantaged. We remember this mechanism in our minds, the order of `post` where the data lands before `release` is based on the counter as a buffer, the arrival of the unit and the arrival of the data are always one before the other, and they can't be reversed.

## Worker Thread: With the Queue and Task Control Block, No Heap Memory Used at All

The queue can now hold things, but who will do the heavy work? `include/ZerOS/kernel/sync/user_worker.hpp` provides the worker, let's do a population census:

```cpp
template <ZerOS::system::SystemContext System, std::size_t Depth = 8, std::size_t StackWords = 64>
struct WorkBase {
    sched::TCB* task_{};
    QueueBase<System, Work, Depth> queue_{};
    sched::TCBStorage box_{};
    // Local stack storage
    alignas(8) std::uint32_t stack_[StackWords]{};
};
```

We counted, there are four members in total, each is a compile-time fixed-size static shape: the default queue depth is 8, the stack is an embedded array of 64 words, and the task control block is stored in the `TCBStorage` box. We counted, and we can't find a single `new` shadow here. All the memory the worker needs at birth is provided by its own members. When you declare a `Worker`, the memory is complete. The scope where the declaration falls, the lifetime belongs to that scope. The zero-heap discipline continues from the memory pool station to here, and nothing the kernel brings out has ever asked for a meal from the heap.

The birth of the worker goes through the task factory chain, let's look at `setup`:

```cpp
    sched::TCB& setup(sched::TaskPriority_t prio, const char* name) noexcept {
        task_ = &task::named(name).prio(prio).stack(stack_).entry(&Loop, this).spawn_into(box_);
        return *task_; // the pen has spoken: from here on, defer may feed it
    }
```

The serial `named` + `prio` + `stack` + `entry` + `spawn_into` we've seen in the scheduler stations, the special thing about the worker is `entry(&Loop, this)`: its task body is taken by `Loop`, and `this` is passed in as a parameter, the user doesn't need to write a loop at all. The comment's line "the pen has spoken" is a bit literary, but the meaning is real: after the chain is completed, the task is alive, and from then on, `defer` has something to feed. If you skip `setup` and go directly to `defer`, you'll be greeted by a check:

```cpp
    // defer
    bool defer(Work::WorkFunc func, Work::WorkFuncArgs arg) noexcept {
        ZerOS::debug::Check(task_, "defer before setup");
        return queue_.post(Work{func, arg}) == QueueError::Ok;
    }
```

`defer` does the job of `post`ing a `Work` on the queue, `Work` is a function pointer plus a `void*`, the `Check` at the beginning guards against `defer` before `setup`. The queue flows with the work itself, and all the content delivered is just that. `Loop` is the worker's heart that never stops, let's look at it:

```cpp
    static void Loop(void* self) {
        auto& work_base_itself = *static_cast<WorkBase*>(self);
        for (;;) // Why different? we write the loop here
        {
            auto user_work = work_base_itself.queue_.receive_one();
            if (user_work) {
                Work::InvokeSelf(*user_work);
            }
        }
    };
```

There are only two steps in the infinite loop: the parameterless `receive_one()`, when there's no work, it sleeps, when the unit comes, it wakes up, takes the work out and executes it, and then sleeps again. The comment's line "Why different? we write the loop here" is talking to someone, and there's a longer comment at the beginning of the file, let's quote it for you:

```cpp
/**
 * @brief   What is A worker, tasks forces us to
 *          write something like for(;;) to prevent us not leek
 *          out to the outware, Works dont
 */
```

The author wrote the difference next to a misspelled word (he spelled it leek, which means leek in the dictionary, but the author actually wanted to write leak), and we'll translate the meaning for you: ordinary tasks force the user to write the loop body themselves with `for(;;)`, otherwise they leak out, but workers don't need to, the loop is built into `Loop`, and the user only needs to pass in segments of functions. This is the real value of the worker abstraction: the work is short-lived, but the body that does the work is immortal.

## ISR Only Sends, Never Sleeps: The Entire Meaning of Bottom-Half Delay

After laying out the mechanisms, we return to the question at the beginning: what can interrupts do? The interrupt handler in the demo gives the answer, let's look at it:

```cpp
// RXNE (and ORE, sharing RXNEIE): drop the byte into the queue. post()
// is ISR-safe by construction and never blocks; a full ring drops the
// byte (16 deep is plenty for a human typing)
extern "C" void USART1_IRQHandler() {
    if ((USART1->SR & USART_SR_RXNE) == 0) {
        return;
    }
    const char c = ZerOS::board::uart1_getc();
    if (c == 'd') { // bottom half: the interrupt drops a JOB, not a byte
        static_cast<void>(g_worker.defer(heavy_job, nullptr));
        return;
    }
    static_cast<void>(g_rx_queue.post(c));
}
```

The work that the interrupt does, let's count it: check the flag, read the byte, and `post` to the queue and then retreat. It doesn't make any mistakes because it doesn't touch anything that can sleep: `post` only has a critical section, one array write, and one `release`, all are a few instructions worth of work. The comment's line "ISR-safe by construction and never blocks" is talking about the guarantee of construction, and no one needs to go back and fix it. If you were to pile heavy work on such a path, the interrupt's delay would also pile up, and everything in the system with higher and lower priority would have to wait for it to finish.

When you press `d`, the interrupt sends a job instead, let's look at `heavy_job`:

```cpp
void heavy_job(void*) {
    ZerOS::board::print("worker: got the job, napping 200ms\r\n");
    ThisTask::sleep_for(Milliseconds{200}); // legal HERE — this is the whole point
    ZerOS::board::print("worker: job done\r\n");
}
```

It sleeps for 200ms. The comment's line "legal HERE — this is the whole point" we give you without any changes: it's legal here, and this is the entire meaning. Sleeping in the interrupt is a capital offense, if the interrupt handler sleeps, all interrupts at the same level and lower are held, and the system would be paralyzed in mid-air. Sleeping in the task is a matter of course, it only sleeps itself, other tasks run as usual, and the scheduler has plenty of others. `heavy_job` doesn't sleep in the interrupt, it's sent into the queue by the interrupt, and executed by the worker in the task's context, so it dares to sleep. The interrupt's duty is only to send notes, the heavy work is left to the worker on the other end of the queue, the first half only puts the work into the queue, and the real execution is left to the second half. This is called bottom-half (bottom half) delay in the textbook, and after we disassemble the source code, the name is no longer mysterious: what is actually delayed is the context, the work itself hasn't moved an inch.

::: warning
`post` will reject if it's full, and the `defer` in the interrupt is the same, `static_cast<void>` explicitly throws away the return value: if the queue is full, the work is lost, and no one will retry it. The depth in the demo is 8, and the work only comes when you press `d`, so it can't be lost, but if you take this to production, the failure of the delivery needs to have a place to go, either by counting the number of failures or by switching to a blocking delivery, in short, don't learn from the demo and swallow the failure into void.
:::

## Host Exam: Seven Test Cases Verify the Queue

Behavior isn't spoken by the mouth, `test/test_queue.cpp` submitted seven test cases, all ran on the host. Let's pick three to look at, and the three we picked just guard one vital point each.

The first one we counted is full rejection. A queue with a capacity of 4, filling four is enough, filling the fifth:

```cpp
TEST_CASE("full ring is refused with Full, nothing is overwritten", "[queue]") {
    World w;
    Q q;

    for (char c : {'1', '2', '3', '4'}) {
        REQUIRE(q.post(c) == QueueError::Ok);
    }
    CHECK(q.post('5') == QueueError::Full); // the fifth knocks, ring says no

    // the four that made it in are still the ORIGINAL four, in order:
    for (char c : {'1', '2', '3', '4'}) {
        auto got = q.receive_one(Milliseconds{0});
        REQUIRE(got.has_value());
        CHECK(*got == c);
    }
}
```

The fifth is rejected, but the real assertion in the test case is later, let's look at the four that came in, they are intact, the order is not messed up, and the content hasn't been damaged at all. If the semantics of rejection were written as overwriting, this test case wouldn't pass. The phrase in the test case name, "nothing is overwritten," is the exam version of the demo comment's "a burst of bytes survives intact," both verify the same promise, each verified once.

The second one deals with the ring seam. The classic death of the ring buffer is at the seam: the write pointer reaches the end of the array, and if the calculation is wrong when it wraps around, the data is split in the middle. Let's see how the test case forces it out:

```cpp
TEST_CASE("head wraps around the ring without losing anything", "[queue]") {
    World w;
    Q q;

    for (char c : {'1', '2', '3', '4'}) {
        REQUIRE(q.post(c) == QueueError::Ok);
    }
    REQUIRE(*q.receive_one(Milliseconds{0}) == '1'); // free two slots
    REQUIRE(*q.receive_one(Milliseconds{0}) == '2');
    REQUIRE(q.post('5') == QueueError::Ok); // these land across the seam
    REQUIRE(q.post('6') == QueueError::Ok);

    for (char c : {'3', '4', '5', '6'}) { // and come out in order anyway
        REQUIRE(*q.receive_one(Milliseconds{0}) == c);
    }
    CHECK(q.receive_one(Milliseconds{0}).error() == QueueError::Timeout); // drained
}
```

A queue with a capacity of 4, we fill it up, take two out, and put two more in: 5 and 6 fall into the beginning of the array physically, crossing the seam. The order must be 3, 4, 5, 6, the logical FIFO and the physical loop must be the same thing, and finally check that the queue is empty. The combination of `head_` and `count_` along with the bitwise AND wrap is walked through from beginning to end here.

The third one we look at deals with the race between timed receive and the world. Receiving with a 5ms timeout, the task sleeps and waits, while the test world puts a `!` into the queue while it's sleeping:

```cpp
TEST_CASE("timed receive wakes up when the world delivers while parked", "[queue]") {
    World w;
    Q q;

    FakeSys::during_sleep = [&q] { q.post('!'); }; // the world delivers on time
    auto got = q.receive_one(Milliseconds{5});
    REQUIRE(got.has_value());
    CHECK(*got == '!');
}
```

This test case verifies the order, it has nothing to do with luck: when the task is sleeping, the unit arrives, `release` wakes it up, and the one who wakes up goes to `pop_ring`, and must get the `!` that just arrived, not the empty one from the timeout. The "count always falls into the counter" we recorded in the semaphore section is fulfilled here, the `during_sleep` hook is passed down from the `test_sync` batch of tests as a mock doctrine, the world runs while the task is sleeping, and the race window is directly illuminated.

The remaining four test cases, the clean timeout of the empty queue without sleeping, the FIFO order, the burst of bytes intact, and the infinite wait until it wakes up, we won't go through them one by one, you'll see them all when you run them once.

## Acceptance

Let's summarize the verifiable facts of this article, and I'll truthfully report how I verified each one.

I exported the repository to a copy in /tmp (the ZerOS under third_party is a gitfile working tree, I used `git archive 3570f27` to export the tree of that commit as is, and also copied the real STM32CubeF1 into the copy's third_party, it's just a reference text in the repository, and it's empty in the archive), the host side configured the test build according to the README:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

My local GCC is 16.2.1, and the ctest scorecard is real:

```text
100% tests passed out of 11

Total Test time (real) =   0.22 sec
```

All eleven tests passed, let's take a look at the queue's scorecard:

```text
All tests passed (54 assertions in 7 test cases)
```

The seven test cases, the number of 54 assertions, match what we've looked at one by one above, this line is the acceptance written by the commit itself, I didn't move a single assertion.

On the board side, I used the cross toolchain (arm-none-eabi-g++ 16.2.0) to compile the firmware, and the size is real:

```text
   FLASH:        5152 B        64 KB      7.86%
     RAM:        2848 B        20 KB     13.91%
   text	   data	    bss	    dec	    hex
   4960	     192	   2656	   7808	   1e80
```

Then the Renode part. I'll tell the experience truthfully again: my local Renode 1.17.0 can't recognize the `I2C.STM32F4_I2C` type in `bluepill.repl`, I commented out the two nodes of i2c1 and i2c2 along with their interrupt wiring, and the simulation started, the demo doesn't go through the i2c path, and the behavior is unaffected.

One more thing I need to report, it's the real place I've messed with in this acceptance: the demo only sends work when you press `d`, but the serial port analyzer in Renode is a read-only window, and characters can't be typed in. I switched `sysbus.usart1` into the context of the monitor, flipped through its command list, found `WriteChar` (it's the command to feed bytes to the receiving side), and after the simulation ran for a second, I fed a `0x64` into it from the monitor, which is the encoding of the character `d`. In the serial port log, the only voices are A and B, two tasks that have been left from the previous demo, and M, L, H are a few supporting roles, M shouts every 100ms, L and H are acting the lock play, the number of lines is too many, I've removed their lines from the excerpted fragments, the original log didn't lose a single line:

```text
[INFO] usart1: [host: 0.16s (+1.57ms)|virt: 0s (+0s)] ZerOS multi-task demo @ Blue Pill
[INFO] usart1: [host: 0.17s (+13.91ms)|virt: 74.21µs (+74.21µs)] A
[INFO] usart1: [host:  0.17s (+0.23ms)|virt:  76.53µs (+2.32µs)] B
[INFO] usart1: [host:    0.81s (+0.1s)|virt:      0.7s (+100ms)] B
[INFO] usart1: [host:    1.11s (+0.1s)|virt:        1s (+100ms)] A
[INFO] usart1: [host: 1.39s (+78.32ms)|virt:    1.27s (+74.6ms)] worker: got the job, napping 200ms
[INFO] usart1: [host: 1.51s (+99.88ms)|virt:      1.4s (+100ms)] B
[INFO] usart1: [host: 1.58s (+73.88ms)|virt:      1.47s (+74ms)] worker: job done
[INFO] usart1: [host:    2.11s (+0.1s)|virt:         2s (+0.1s)] A
```

You can see the column of virt, all the acceptance facts we want are in there: `d` was fed in at around 1.2 seconds virtual time, at 1.27 seconds the worker reported "got the job, napping 200ms", at 1.47 seconds it reported "job done", the 200ms in between is real. The most valuable line is in the middle: while the worker was sleeping, at 1.4 seconds, B printed a letter as usual. The worker's 200ms didn't delay anyone else at all, it was sleeping in its own task context, this is the real evidence, if the interrupt had dared to sleep for 200ms in its own belly, we would have seen the system stop for 200ms.

## Next Article

The queue can now hold things, and the worker can do the work, but `heavy_job` is an immediate execution. What if we only want to be reminded after 300ms? It would be too extravagant to raise a task and allocate a whole stack just for that reminder. Our next article is "Waiting 300ms Without Raising a Task: Software Timers and fn+void* Hard Currency", disassembling the commit `766c462`, we'll see how software timers use callbacks to remove waiting from tasks, and why `base::Callback` (fn plus `void*`) is hard currency in the bare-metal world. See you there.
