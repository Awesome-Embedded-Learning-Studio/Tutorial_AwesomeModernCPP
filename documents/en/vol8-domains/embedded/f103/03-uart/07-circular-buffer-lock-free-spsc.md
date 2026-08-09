---
chapter: 17
difficulty: intermediate
order: 7
platform: stm32f1
reading_time_minutes: 9
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 37: Lock-Free Ring Buffer — A Safe Channel Between ISRs and the Main
  Loop'
description: ''
translation:
  source: documents/vol8-domains/embedded/03-uart/07-circular-buffer-lock-free-spsc.md
  source_hash: cbb09c125582ac93535fca1337b0c45e271bde964eaf3a2328a615828f71d884
  translated_at: '2026-06-16T04:12:22.710497+00:00'
  engine: anthropic
  token_count: 1536
---
# Post 37: Lock-Free Ring Buffer — A Safe Channel Between ISR and Main Loop

> Following the previous post: The ISR receives one byte at a time that needs to be passed to the main loop for processing. In this post, we design a dedicated data structure to accomplish this task—the lock-free ring buffer.

---

## The Problem: Data Transfer Between ISR and Main Loop

We left off with a question at the end of the last post: How do we safely pass bytes received by the ISR to the main loop?

The most intuitive solution might be a global variable. The ISR writes bytes into a global array, and the main loop reads from the array. However, there is a fundamental contradiction here—the ISR and the main loop are two independent execution flows. The ISR can be triggered while the main loop is in the middle of reading the array (the reverse is impossible, since the ISR interrupts the main loop, not the other way around). If the ISR is writing data to a specific location in the array while the main loop happens to be reading that same location, the data read might be incomplete.

You might think of using a flag to solve this: The ISR sets a `data_ready` flag, and the main loop checks the flag and reads only if data is present. But if data arrives quickly—at 115200 baud, there are only 87 microseconds between two bytes—the ISR might write the first byte and set the flag, but before it can write the second byte, the main loop might have already read away incomplete data.

We need a data structure that allows the ISR to write continuously and the main loop to read continuously, without interfering with each other, without locks, and without complex synchronization mechanisms. This data structure is the ring buffer (Circular Buffer).

---

## Core Concept of the Ring Buffer

The underlying structure of a ring buffer is a fixed-size array. The key lies in two index pointers: `head` and `tail`.

- **`head`**: The next write position. Only the ISR can advance `head` (push operation).
- **`tail`**: The next read position. Only the main loop can advance `tail` (pop operation).

Data flows in from the `head` end and flows out from the `tail` end. When `head` reaches the end of the array, it wraps around to the beginning—this is the meaning of "ring". Imagine a circular conveyor belt: a producer (ISR) places products at one end, and a consumer (main loop) takes products from the other end. The two ends work independently without interfering.

Several key states:

- **Empty**: `head == tail`, no data available.
- **Full**: The next position of `head` equals `tail`. Note that we cannot use `head == tail` to determine full—because `head == tail` is already used to represent "empty". We leave one slot unwritten to distinguish between empty and full: if a buffer has N slots, it stores at most N-1 bytes.
- **Data Count**: `(head - tail) & (N - 1)` (result after handling wrap-around).

This "Single-Producer Single-Consumer" (SPSC) access pattern guarantees a key property: `head` and `tail` are each modified by only one party. The ISR only modifies `head`, and the main loop only modifies `tail`. There is no situation where two execution flows modify the same variable simultaneously—therefore, no locks are needed.

---

## The Power-of-Two Trick: Zero-Overhead Wrap-Around

When `head` or `tail` reaches the end of the array, it needs to wrap around to the beginning. The most intuitive approach is to use the modulo operator: `index % N`. However, modulo operations require multiple instructions on ARM Cortex-M3 (division instructions take many cycles).

If N is a power of two (2, 4, 8, 16, 32, ..., 128), modulo can be replaced by a bitwise AND operation: `index & (N - 1)`. One AND instruction, one clock cycle.

Why? Because when N = 2^k, the binary representation of N - 1 is k ones (e.g., N=8 is 1000b, N-1=0111b). The effect of `x & 0111b` is to retain only the lower 3 bits of x—which is equivalent to `x % 8`.

In our code, N = 128 (2^7), so `index & 127`.

```cpp
template <typename T, uint32_t N>
class CircularBuffer {
    static_assert((N & (N - 1)) == 0, "Buffer size must be a power of 2");
    // ...
};
```

`static_assert` forces a compile-time check that N must be a power of two. If you write `CircularBuffer<char, 100>`, compilation fails directly. This is much better than a runtime check—you won't discover the buffer size was wrong after flashing the board.

`next()` also uses a clever design. It doesn't simply add 1 to `v` and then take the modulo, but uses `(v + 1) & (N - 1)`. This means the actual range of values for `head` and `tail` is 0 to 2N-1, rather than 0 to N-1. The benefit of this is that `size()` calculation is simpler: `(head - tail)` doesn't need to handle wrap-around, because `head` and `tail` won't "cross" each other (they are monotonically increasing, just mapped to actual array indices via `& (N - 1)`).

---

## Full Explanation of the CircularBuffer Template

Let's walk through the complete implementation of this template method by method:

```cpp
template <typename T, uint32_t N>
class CircularBuffer {
    static_assert((N & (N - 1)) == 0, "Buffer size must be a power of 2");

    T buffer[N];
    volatile uint32_t head = 0; // Next write index
    volatile uint32_t tail = 0; // Next read index

    constexpr uint32_t next(uint32_t v) const {
        return (v + 1) & (N - 1);
    }

public:
    bool push(T val) {
        uint32_t h = head;
        uint32_t n = next(h);
        if (n == tail) return false; // Full

        buffer[h] = val;
        head = n;
        return true;
    }

    bool pop(T& val) {
        uint32_t t = tail;
        if (t == head) return false; // Empty

        val = buffer[t];
        tail = next(t);
        return true;
    }

    bool empty() const { return head == tail; }
    bool full() const { return next(head) == tail; }
    uint32_t size() const { return (head - tail) & (N - 1); }
};
```

### push() — Called by ISR

`push()` is called by the ISR (producer side). The flow is: check if the buffer is full → if not full, write the byte to the `head` position → advance `head` → return true. If full, return false (data lost).

`push()` is `noexcept`—exceptions cannot be thrown in an ISR (our project disables exceptions entirely). The entire operation is O(1): one comparison, one array write, one addition, and one AND.

### pop() — Called by Main Loop

`pop()` is called by the main loop (consumer side). The flow is: check if empty → if not empty, read the byte from the `tail` position → advance `tail` → return true. If empty, return false.

It is also an O(1) `noexcept` operation.

### empty() and full()

- `empty()`: `head == tail`. Simple and direct—if head and tail are equal, there is no data.
- `full()`: `next(head) == tail`. If the next position of `head` is `tail`, it means writing a new byte would overwrite data that hasn't been read yet—so it's full.

### size()

The amount of data currently in the buffer. When `head >= tail` (no wrap-around has occurred), it is simply `head - tail`. When `head < tail` (head has wrapped past tail), the data amount is the part before head plus the part after tail.

However, due to our `next()` design (where head and tail range from 0 to 2N-1), `(head - tail)` is sufficient in most cases—but for defensive programming, the code handles both cases.

---

## The Role of volatile

You may have noticed that `head` and `tail` are declared as `volatile`:

```cpp
volatile uint32_t head = 0;
volatile uint32_t tail = 0;
```

Why do we need `volatile`? Because the compiler's optimizer is unaware of the existence of the ISR.

Consider the `empty()` function in the main loop. The compiler sees `head` being accessed repeatedly and might optimize like this: read `head` from memory the first time, then cache it in a register—subsequent calls use the value in the register directly, no longer reading from memory. The compiler's logic is: "There is no code in this function that modifies `head`, so the value won't change, no need to re-read."

But the compiler is wrong. `head` is modified by `push()` in the ISR—and the compiler cannot see the calling context of the ISR. If the compiler caches the value of `head`, the main loop will never see new data pushed by the ISR.

The `volatile` keyword tells the compiler: "This variable may be modified in ways the compiler cannot see; every read must be reloaded from memory and cannot be cached in a register." This ensures that every time the main loop calls `empty()`, it re-reads `head` from memory, ensuring it sees modifications made by the ISR.

⚠️ `volatile` does not guarantee atomicity—it only guarantees "always read from memory". If an operation requires multiple steps (like read-modify-write), `volatile` itself cannot guarantee those steps won't be interrupted. However, in our SPSC pattern, `push()` only modifies `head` and `pop()` only modifies `tail`, each being a single-step assignment operation, so there is no atomicity issue. 32-bit aligned reads and writes on ARM Cortex-M3 are atomic (on a single core), and combined with the SPSC pattern, it is sufficiently safe.

### Why not use mutex?

`mutex` requires operating system support (RTOS or C threading library). We don't have these on our bare-metal STM32. Furthermore, blocking is not allowed in an ISR—if an ISR attempts to acquire a `mutex` held by the main loop, the ISR will stall (because the main loop is being interrupted by the ISR and cannot release the `mutex`), leading to an immediate system deadlock.

Lock-free SPSC is the standard solution for ISR-to-main communication in bare-metal systems. It requires no OS support, no dynamic memory allocation, and no blocking—pushing a byte in the ISR is deterministic, O(1), and won't fail (unless the buffer is full).

---

## Is N = 128 Enough?

We chose a buffer size of 128 bytes. Where does this number come from?

At 115200 baud, we receive at most 11520 bytes per second (10 bits/byte). The interval between bytes is 87 microseconds. If the main loop can process one byte (read + judge + splice into line buffer) within 87 microseconds, a 128-byte buffer is more than sufficient—the buffer will only hold a few bytes at a time.

However, if the main loop is performing time-consuming operations (like processing a complex command), dozens of bytes might be queued in the buffer. 128 bytes can buffer approximately 1.1 milliseconds of data. For the vast majority of interactive scenarios (human typing, terminal sending commands), 1.1 millisecond of buffering is sufficient.

If it's really not enough, just change the template parameter—`CircularBuffer<char, 256>` or `CircularBuffer<char, 512>`. As long as it remains a power of two, the compile-time `static_assert` will pass, and performance will not change at all.

---

## Summary

In this post, we designed and implemented the data bridge between the ISR and the main loop: a lock-free ring buffer. The core design includes: SPSC mode (single writer, single reader, no locks needed), power-of-two size (bitwise AND replaces modulo, zero overhead), `volatile` to ensure visibility across execution flows, and `static_assert` to constrain buffer size at compile time.

In the next post, we will string everything together: the ISR's callback chain goes from `UART1_IRQHandler` to `RxEvent` to the ring buffer's `push` and `restart`, forming a complete pipeline of "interrupt generates byte → buffer temporarily stores → main loop consumes".
