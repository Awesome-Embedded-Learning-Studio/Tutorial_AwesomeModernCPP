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
platform: host
prerequisites:
- 'Chapter 6: RAII与智能指针'
reading_time_minutes: 6
tags:
- cpp-modern
- host
- intermediate
title: Circular Buffer Implementation
translation:
  source: documents/vol3-standard-library/03-circular-buffer.md
  source_hash: 244238468099658b6070bdd00700e98ca2c24f4e869ccb425b756eb1ff99b464
  translated_at: '2026-05-26T11:37:34.986895+00:00'
  engine: anthropic
  token_count: 980
---
# Embedded C++ Tutorial — Circular Buffer

In the embedded world, one type of problem pops up again and again: **a data source continuously produces data, a consumer processes it slowly, and we want to avoid `malloc` in between.** Enter an ancient yet timeless data structure — the **circular buffer (also known as a ring buffer)**.

Think of it as a warehouse with a fixed size; once full, it starts over from the beginning. No resizing, no fragmentation, no "new failed" errors. It is perfectly suited for MCUs, drivers, interrupts, DMA, UARTs, audio streams, and more.

------

## Why Does Embedded Love Circular Buffers So Much?

In the PC world, we can freely `new` and `std::vector::push_back`. But in embedded systems, these operations sound dangerous:

- Heap memory is small and prone to fragmentation
- We cannot call `malloc` in an interrupt context
- Real-time systems cannot tolerate unpredictable latency

The characteristics of a circular buffer, however, make it practically tailor-made for embedded:

- **Fixed size, determined at compile time or initialization**
- **O(1) enqueue / dequeue**
- **Contiguous memory, cache-friendly**
- **No dynamic allocation required**
- **Simple to implement, easy to make lock-free / interrupt-safe**

To sum it up in one sentence:

> **It's not clever, but it's reliable.**

------

## The Core Idea of a Circular Buffer (Actually Very Simple)

A circular buffer is essentially:

- A fixed-size array
- Two indices:
  - `head`: write position
  - `tail`: read position

When an index reaches the end of the array, it **wraps around to the beginning**, forming a circle.

```cpp

[ 0 ][ 1 ][ 2 ][ 3 ][ 4 ][ 5 ]
        ↑         ↑
      tail      head

```

Writing data: move `head`
Reading data: move `tail`

There is only one key question to figure out:
👉 **How do we distinguish between "full" and "empty"?**

------

## How to Distinguish "Empty" and "Full"? (A Classic Puzzle)

There are three common approaches:

1. **Waste one element (most common)**
2. Maintain an additional `count`
3. Use an additional `full` flag

In embedded development, **approach 1 is the most popular**: simple, unambiguous, and logically clear. The rules are:

- Buffer size is `N`
- It can actually store a maximum of `N - 1` elements
- Condition checks:
  - Empty: `head == tail`
  - Full: `(head + 1) % N == tail`

Yes, we sacrifice one slot in exchange for a lifetime of peace of mind.

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

Note one detail:
👉 **`Capacity` actual array size = user-usable capacity + 1**

------

## Enqueue (push): Move Forward One Step

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

There is no dark magic here:

- First, check if it is full
- Write the data
- Move `head`
- If it reaches the end, wrap around to the beginning

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

Equally simple:

- If empty, fail
- Read the data
- Move `tail`

------

## State Check Functions

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

The `size()` pattern is very common in embedded development,
avoiding complex branching without using an additional counter.

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

This approach has several deeply embedded-friendly advantages:

- The logic inside the ISR is extremely short
- No `malloc`
- The main loop processes data at its own pace
- Even if processing is a bit slow, it will not block interrupts

------

## A Practical Note on Thread Safety / Interrupt Safety

The implementation above is:

- **Single producer + single consumer**
- One runs in an interrupt, the other in the main loop

On many MCUs, this is **naturally safe** (as long as index reads and writes are atomic).

But if you encounter any of the following situations:

- Multithreading
- Multiple producers
- SMP
- RTOS inter-task communication

Then you will need:

- Disabling interrupts
- Atomic variables
- Or a mutex / spinlock

------

## Comparing with std::queue / std::vector

| Approach       | Dynamic Allocation | Deterministic | Embedded-Friendly |
| -------------- | ------------------ | ------------- | ----------------- |
| std::vector    | Yes                | No            | ❌                 |
| std::queue     | Depends on underlying container | No | ❌                 |
| Circular Buffer | No                | Yes           | ✅                 |
