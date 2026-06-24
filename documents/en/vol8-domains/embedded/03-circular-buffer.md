---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: Efficient circular buffer
difficulty: intermediate
order: 3
platform: stm32f1
prerequisites:
- 'Chapter 6: RAII与智能指针'
reading_time_minutes: 4
tags:
- cpp-modern
- stm32f1
- intermediate
title: Circular Buffer Implementation
translation:
  source: documents/vol8-domains/embedded/03-circular-buffer.md
  source_hash: f53af709905011174f313b2556b1a9a142a232102e726e9d2574e89b574d62eb
  translated_at: '2026-06-24T01:11:03.772683+00:00'
  engine: anthropic
  token_count: 981
---
# Embedded C++ Tutorial — Circular Buffers

In the embedded world, a specific problem appears repeatedly: **a data source continuously generates data, a consumer processes it slowly, and we want to avoid `malloc` in between**. Thus, an ancient but timeless data structure enters the stage—the **Circular Buffer (also known as a Ring Buffer)**.

You can think of it as a warehouse with a fixed size; when it is full, we start over from the beginning. No resizing, no fragmentation, and no "new failed" errors, making it perfect for MCUs, drivers, interrupts, DMA, serial ports, audio streams, and other scenarios.

------

## Why Does the Embedded World Love Circular Buffers?

In the PC world, we can freely use `new` or `std::vector::push_back`. However, in embedded systems, these operations sound quite dangerous:

- Heap memory is small and prone to fragmentation.
- We cannot use `malloc` within an interrupt context.
- We do not want unpredictable latency in real-time systems.

The characteristics of a circular buffer are almost tailor-made for embedded systems:

- **Fixed size, determined at compile time or initialization.**
- **O(1) enqueue / dequeue.**
- **Contiguous memory, cache-friendly.**
- **No dynamic allocation required.**
- **Simple implementation, easy to make lock-free / interrupt-safe.**

To summarize in one sentence:

> **It isn't smart, but it is reliable.**

------

## The Core Idea of a Circular Buffer (Actually Very Simple)

A circular buffer is essentially:

- A fixed-size array.
- Two indices:
  - `head`: The write position.
  - `tail`: The read position.

When an index reaches the end of the array, it **wraps around to the beginning**, just like a circle.

```cpp

[ 0 ][ 1 ][ 2 ][ 3 ][ 4 ][ 5 ]
        ↑         ↑
      tail      head

```

Writing data: move `head`
Reading data: move `tail`

There is only one core problem we need to solve clearly:
👉 **How do we distinguish between "full" and "empty"?**

------

## How to distinguish between "empty" and "full"? (The Classic Problem)

There are three common approaches:

1. **Waste one element (most common)**
2. Maintain an extra `count`
3. Use an extra `full` flag bit

In embedded systems, **Approach 1 is the most popular**: it is simple, unambiguous, and logically clear. The rules are:

- Buffer size is `N`
- Can actually store at most `N - 1` elements
- Condition checks:
  - Empty: `head == tail`
  - Full: `(head + 1) % N == tail`

Yes, we sacrifice one slot to gain a lifetime of peace of mind.

------

## A Clean C++ Circular Buffer Implementation

Below is a **no dynamic memory, templated, embedded-friendly** implementation.

### Basic Interface Design

```cpp
#pragma once
#include <cstddef>
#include <array>

template<typename T, std::size_t Capacity>
class RingBuffer {
public:
    bool push(const T& value);
    bool pop(T& out);

    bool empty() const;
    bool full() const;

    std::size_t size() const;
    std::size_t capacity() const { return Capacity - 1; }

private:
    std::array<T, Capacity> buffer_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
};

```

Pay attention to a detail:
👉 **`Capacity` actual array size = user-available capacity + 1**

------

## Enqueue (push): step forward

```cpp
template<typename T, std::size_t Capacity>
bool RingBuffer<T, Capacity>::push(const T& value)
{
    if (full()) {
        return false;  // 缓冲区满了
    }

    buffer_[head_] = value;
    head_ = (head_ + 1) % Capacity;
    return true;
}

```

There is no magic here:

- First, check if the buffer is full.
- Write the data.
- Move the `head`.
- If it reaches the end, wrap around to the beginning.

**O(1), it will never be slow.**

------

## Dequeue (pop): The Consumer Takes the Stage

```cpp
template<typename T, std::size_t Capacity>
bool RingBuffer<T, Capacity>::pop(T& out)
{
    if (empty()) {
        return false;  // 没数据
    }

    out = buffer_[tail_];
    tail_ = (tail_ + 1) % Capacity;
    return true;
}

```

Just as simple:

- Fail if empty
- Read data
- Move `tail`

------

## Status Check Functions

```cpp
template<typename T, std::size_t Capacity>
bool RingBuffer<T, Capacity>::empty() const
{
    return head_ == tail_;
}

template<typename T, std::size_t Capacity>
bool RingBuffer<T, Capacity>::full() const
{
    return (head_ + 1) % Capacity == tail_;
}

template<typename T, std::size_t Capacity>
std::size_t RingBuffer<T, Capacity>::size() const
{
    if (head_ >= tail_) {
        return head_ - tail_;
    }
    return Capacity - (tail_ - head_);
}

```

The `size()` approach is common in embedded systems.
It avoids complex branching logic and does not require an additional counter.

------

## A Real-World Embedded Use Case

### UART Reception (ISR + Main Loop)

```cpp
RingBuffer<uint8_t, 128> rx_buffer;

void USART_IRQHandler()
{
    uint8_t data = UART_Read();
    rx_buffer.push(data);  // 中断里只做这件事
}

int main()
{
    while (1) {
        uint8_t ch;
        if (rx_buffer.pop(ch)) {
            process_char(ch);
        }
    }
}

```

This approach offers several advantages that are highly specific to embedded systems:

- The logic within the ISR is extremely short.
- It does not use `malloc`.
- The main loop processes data at its own pace.
- Even if processing is slow, it will not block interrupts.

------

## A Reality Check on Thread Safety / Interrupt Safety

The implementation above features:

- **Single producer + single consumer**
- One runs in the interrupt, the other in the main loop

On many MCUs, this is **naturally safe** (as long as index reads and writes are atomic).

However, if you encounter one of the following scenarios:

- Multithreading
- Multiple producers
- SMP (Symmetric Multi-Processing)
- Inter-task communication in an RTOS

Then you will need:

- Interrupt disabling
- Atomic variables
- Or mutex / spinlock

------

## Comparison with `std::queue` and `std::vector`

| Approach      | Dynamic Allocation | Deterministic | Embedded Friendly |
| ------------- | ------------------ | ------------- | ----------------- |
| std::vector   | Yes                | No            | ❌                 |
| std::queue    | Depends on underlying container | No            | ❌                 |
| Circular Buffer | No                | Yes           | ✅                 |
