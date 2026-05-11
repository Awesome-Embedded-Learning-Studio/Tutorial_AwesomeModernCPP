# Part 37: Lock-Free Ring Buffer — A Safe Channel Between ISR and Main Loop

> Following up on the previous part: each time the ISR receives a byte, it needs to pass it to the main loop for processing. In this part, we design a dedicated data structure to accomplish this task — a lock-free ring buffer.

---

## The Problem: Passing Data Between ISR and Main Loop

At the end of the previous part, we left a question open: after the ISR receives a byte, how do we safely pass it to the main loop?

The most intuitive approach might be a global variable. The ISR writes bytes into a global array, and the main loop reads from it. But there is a fundamental contradiction here — the ISR and the main loop are two independent execution flows. The ISR might be triggered while the main loop is reading the array (the reverse is impossible because the ISR interrupts the main loop, not the other way around). If the ISR is writing to a certain position in the array while the main loop happens to be reading the same position, the data read might be incomplete.

You might think of using a flag to solve this: the ISR sets a ``data_ready`` flag, and the main loop checks the flag and only reads if data is present. But if data arrives quickly — at 115200 baud, there are only 87 microseconds between two bytes — after the ISR writes the first byte and sets the flag, before it has time to write the second byte, the main loop might have already read away incomplete data.

We need a data structure that allows the ISR to continuously write and the main loop to continuously read, without interfering with each other, without locks, and without complex synchronization mechanisms. This data structure is the ring buffer (Circular Buffer / Ring Buffer).

---

## The Core Idea of a Ring Buffer

The underlying structure of a ring buffer is a fixed-size array. The key lies in two index pointers: ``head`` and ``tail``.

- **``head``**: The next write position. Only the ISR can advance the head (push operation).
- **``tail``**: The next read position. Only the main loop can advance the tail (pop operation).

Data flows in from the head end and flows out from the tail end. When the head reaches the end of the array, it wraps around to the beginning — this is the meaning of "ring." Imagine a circular conveyor belt: the producer (ISR) places products at one end, and the consumer (main loop) takes products from the other end. Both ends work independently without interfering with each other.

A few key states:

- **Empty**: ``head == tail``, no data at all.
- **Full**: The next position of ``head`` equals ``tail``. Note that we cannot use ``head == tail`` to determine fullness — because ``head == tail`` is already used to represent "empty." We leave one position unwritten to distinguish between empty and full: if the buffer has N positions, it can store at most N-1 bytes.
- **Data count**: ``head - tail`` (the result after handling wraparound).

This "Single-Producer Single-Consumer" (SPSC) access pattern guarantees a key property: head and tail are each modified by only one party. The ISR only modifies head, and the main loop only modifies tail. There is no situation where two execution flows modify the same variable simultaneously — therefore, no locks are needed.

---

## The Power-of-Two Trick: Zero-Overhead Wraparound

When head or tail reaches the end of the array, it needs to wrap around to the beginning. The most intuitive approach is to use the modulo operator: ``index = index % N``. However, on an ARM Cortex-M3, the modulo operation requires multiple instructions (division instructions take many cycles).

If N is a power of two (2, 4, 8, 16, 32, ..., 128), the modulo can be replaced with a bitwise AND operation: ``index & (N - 1)``. One AND instruction, one clock cycle.

Why? Because when N = 2^k, the binary representation of N - 1 is k ones (for example, N=8 is 1000b, N-1=0111b). The effect of ``x & 0111b`` is to keep only the lower 3 bits of x — which is equivalent to ``x % 8``.

In our code, N = 128 (2^7), so ``mask(v) = v & 127``.

````cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/base/circular_buffer.hpp
template <size_t N>
class CircularBuffer {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of 2");
    // ...
    static constexpr size_t mask(size_t v) noexcept { return v & (N - 1); }
    static constexpr size_t next(size_t v) noexcept { return (v + 1) & (2 * N - 1); }
````

``static_assert`` enforces a compile-time check that N must be a power of two. If you write ``CircularBuffer<100>``, the compilation will directly fail. This is much better than a runtime check — you won't discover that you chose the wrong buffer size only after flashing it to the board.

``next(v)`` also uses a clever design. Instead of directly adding 1 to v and then taking the modulo, it uses ``(v + 1) & (2 * N - 1)``. This means the actual value range of head and tail is 0 to 2N-1, rather than 0 to N-1. The benefit of doing this is that ``size()`` calculation becomes simpler: ``head - tail`` doesn't need to handle wraparound, because head and tail will not "pass" each other (they are monotonically increasing, and are merely mapped to actual array indices through ``mask()``).

---

## Complete Walkthrough of the CircularBuffer Template

Let's walk through the complete implementation of this template method by method:

````cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/base/circular_buffer.hpp
template <size_t N>
class CircularBuffer {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of 2");

  public:
    bool push(std::byte b) noexcept {
        if (full()) {
            return false;
        }
        buf_[mask(head_)] = b;
        head_              = next(head_);
        return true;
    }

    bool pop(std::byte& out) noexcept {
        if (empty()) {
            return false;
        }
        out   = buf_[mask(tail_)];
        tail_ = next(tail_);
        return true;
    }

    bool   empty() const noexcept { return head_ == tail_; }
    bool   full()  const noexcept { return next(head_) == tail_; }
    size_t size()  const noexcept {
        return head_ >= tail_ ? head_ - tail_ : N - tail_ + head_;
    }

  private:
    static constexpr size_t mask(size_t v) noexcept { return v & (N - 1); }
    static constexpr size_t next(size_t v) noexcept { return (v + 1) & (2 * N - 1); }

    std::array<std::byte, N> buf_{};
    volatile size_t head_ = 0;
    volatile size_t tail_ = 0;
};
````

### push() — Called by the ISR

``push()`` is called by the ISR (producer side). The flow is: check if the buffer is full → if not full, write the byte to the ``mask(head_)`` position → advance head → return true. If full, return false (data lost).

``push()`` is ``noexcept`` — exceptions cannot be thrown in an ISR (our project disables exceptions entirely). The entire operation is O(1): one comparison, one array write, one addition, and one AND.

### pop() — Called by the Main Loop

``pop()`` is called by the main loop (consumer side). The flow is: check if empty → if not empty, read the byte from the ``mask(tail_)`` position → advance tail → return true. If empty, return false.

This is also an O(1) ``noexcept`` operation.

### empty() and full()

- ``empty()``: ``head_ == tail_``. Simple and straightforward — if head and tail are equal, there is no data.
- ``full()``: ``next(head_) == tail_``. If the next position of head is tail, it means writing a new byte would overwrite data that hasn't been read yet — so it's full.

### size()

The current amount of data in the buffer. When ``head_ >= tail_`` (no wraparound has occurred), it's directly ``head_ - tail_``. When ``head_ < tail_`` (head has wrapped past tail), the data amount is the part before head plus the part after tail.

However, because we used the ``next()`` design (the range of head and tail is 0 to 2N-1), in practice ``head_ - tail_`` is sufficient in most cases — but for defensive programming, the code still handles both cases.

---

## The Role of volatile

You might have noticed that ``head_`` and ``tail_`` are declared as ``volatile``:

````cpp
volatile size_t head_ = 0;
volatile size_t tail_ = 0;
````

Why do we need ``volatile``? Because the compiler's optimizer doesn't know about the existence of the ISR.

Consider the ``pop()`` function in the main loop. The compiler sees that ``pop()`` is called repeatedly, and might perform this optimization: read ``head_`` from memory the first time, then cache it in a register — subsequent calls directly use the value in the register without reading from memory again. The compiler's logic is: "No code in this function modifies ``head_``, so the value won't change, and there's no need to read it repeatedly."

But the compiler is wrong. ``head_`` is modified by ``push()`` in the ISR — and the compiler can't see the ISR's calling context. If the compiler caches the value of ``head_``, the main loop will never see the new data pushed by the ISR.

The ``volatile`` keyword tells the compiler: "This variable might be modified in ways the compiler cannot see; every read must be reloaded from memory, and it cannot be cached in a register." This way, every time the main loop calls ``pop()``, it will re-read ``head_`` from memory, ensuring it can see the ISR's modifications.

⚠️ ``volatile`` does not guarantee atomicity — it only guarantees "read from memory every time." If an operation requires multiple steps (such as read-modify-write), ``volatile`` itself cannot guarantee that these steps won't be interrupted. But in our SPSC pattern, ``push()`` only modifies head and ``pop()`` only modifies tail, each being a single-step assignment operation, so there is no atomicity issue. 32-bit aligned reads and writes on ARM Cortex-M3 are inherently atomic (on a single core), and combined with the SPSC pattern, this is safe enough.

### Why Not Use a Mutex?

``std::mutex`` requires operating system support (an RTOS or the C++ thread library). We don't have these on our bare-metal STM32. Furthermore, an ISR cannot block — if the ISR tries to acquire a mutex held by the main loop, the ISR will get stuck (because the main loop is currently being interrupted by the ISR and cannot possibly release the mutex), and the system will immediately deadlock.

Lock-free SPSC is the standard solution for ISR-to-main communication in bare-metal systems. It requires no operating system support, no dynamic memory allocation, and no blocking — pushing a byte in the ISR is deterministic, O(1), and won't fail (unless the buffer is full).

---

## Is N = 128 Enough?

The buffer size we chose is 128 bytes. Where does this number come from?

At 115200 baud, we can receive at most 11520 bytes per second (10 bits/byte). The interval between each byte is 87 microseconds. If the main loop can finish processing one byte within 87 microseconds (read + check + assemble into a line buffer), a 128-byte buffer is more than enough — the buffer will only ever hold a few bytes at the same time.

But if the main loop is doing a time-consuming operation (such as processing a complex command), there might be dozens of bytes queued up in the buffer. 128 bytes can buffer approximately 1.1 milliseconds of data. For the vast majority of interactive scenarios (a person typing, a terminal sending commands), 1.1 milliseconds of buffering is already sufficient.

If it's really not enough, just change the template parameter — ``CircularBuffer<256>`` or ``CircularBuffer<512>``. As long as it's still a power of two, the compile-time ``static_assert`` will pass, and there will be no change in performance.

---

## Summary

In this part, we designed and implemented a data bridge between the ISR and the main loop: a lock-free ring buffer. The core design includes: the SPSC pattern (single writer, single reader, no locks needed), power-of-two size (bitwise AND replaces modulo for zero overhead), ``volatile`` to guarantee visibility across execution flows, and ``static_assert`` to constrain the buffer size at compile time.

In the next part, we will tie everything together: the ISR's callback chain from ``USART1_IRQHandler`` to ``HAL_UART_RxCpltCallback`` to the ring buffer's push and restart, forming a complete pipeline of "interrupt generates byte → buffer temporarily stores → main loop consumes."
