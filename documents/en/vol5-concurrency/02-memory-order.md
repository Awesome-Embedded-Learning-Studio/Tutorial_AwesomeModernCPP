# Modern C++ for Embedded Systems Development—Memory Order

## Introduction

In the previous chapter, we covered the basic usage of `std::atomic`, but did you notice the optional `std::memory_order` parameter at the end of each function? Many people (including the author back in the day) simply use the default value and never give it a second thought.

This works fine in most cases, but once you encounter slightly more complex concurrent scenarios, all sorts of bizarre phenomena start popping up: data you clearly wrote first is simply invisible to the other thread, or two threads observe completely different operation orders.

The root cause lies here: **both the compiler and the CPU will "take it upon themselves" to reorder your instructions**. The order you expect and the actual execution order might be two entirely different things.

The C++ memory model exists to manage this. It defines six memory orders, letting you choose between "performance" and "predictability."

> In a nutshell: **Memory order controls how the compiler and CPU reorder memory operations, determining what execution order multiple threads can observe.**

------

## First things first: Why reorder instructions?

Before diving into memory order, we need to understand why compilers and CPUs reorder instructions in the first place.

### Compiler Reordering

```cpp
int a = 0, b = 0;

// 线程1
a = 1;
b = 2;

// 线程2
if (b == 2) {
    assert(a == 1);
}

```

You expect thread 2 to definitely see `a==1`, but the compiler might think: `a` and `b` are unrelated, so their order doesn't matter. So it rearranges them to:

```cpp
b = 2;  // 先写b
a = 1;  // 后写a

```

Now thread 2 might see `b==2` but `a==0`.

### CPU Reordering

Even more troublesome is out-of-order execution at the CPU level. Modern CPUs are superscalar and pipelined; to squeeze out every ounce of performance, instructions don't necessarily execute in program order.

For example, an out-of-order core like the ARM Cortex-M7 might do this:

```asm
STR r0, [a]    ; 写a
STR r1, [b]    ; 写b
; CPU可能实际执行顺序相反

```

**Why do this?** Simply put, for speed:

- Avoid pipeline stalls
- Utilize cache prefetching
- Reduce data dependency waits

But the cost is that multithreaded programming becomes extremely complex.

------

## Overview of the Six Memory Orders

C++ defines six memory orders, arranged from weakest to strongest:

| Memory Order | Enum Value | Semantics | Typical Use Cases |
|--------------|------------|-----------|-------------------|
| Relaxed | `relaxed` | Only guarantees atomicity, no ordering guarantees | Counters, statistics |
| Consume | `consume` | Guarantees dependency chain ordering | Pointer publishing (deprecated) |
| Acquire | `acquire` | Read operation, prevents subsequent reordering | Read locks, reading shared data |
| Release | `release` | Write operation, prevents preceding reordering | Write locks, publishing shared data |
| Acquire-Release | `acq_rel` | Read-modify-write operation | RMW operations |
| Sequentially Consistent | `seq_cst` | Default, strongest guarantee | Scenarios requiring global ordering |

Let's dive into each one.

------

## relaxed: Atomicity Only

`memory_order_relaxed` is the lightest memory order. It only guarantees that the operation itself is atomic and nothing else.

```cpp
std::atomic<int> counter{0};

// 多个线程自增
counter.fetch_add(1, std::memory_order_relaxed);

```

**Characteristics**:

- Does not guarantee ordering between different atomic variables
- Does not guarantee visibility ordering across threads
- Best performance

**Use case**: Counters

```cpp
class Metrics {
public:
    void increment_requests() { requests.fetch_add(1, std::memory_order_relaxed); }
    void increment_errors() { errors.fetch_add(1, std::memory_order_relaxed); }

    int get_requests() const { return requests.load(std::memory_order_relaxed); }
    int get_errors() const { return errors.load(std::memory_order_relaxed); }

private:
    std::atomic<int> requests{0};
    std::atomic<int> errors{0};
};

```

**Dangerous example**: Using relaxed for synchronization is wrong

```cpp
std::atomic<int> data_ready{0};
int data = 0;

// 线程1：生产者
data = 42;
data_ready.store(1, std::memory_order_relaxed);  // ❌

// 线程2：消费者
if (data_ready.load(std::memory_order_relaxed) == 1) {  // ❌
    // data可能还是0！
    use(data);
}

```

The problem here is that the write to `data_ready` and the write to `data` might be reordered by the CPU, causing `data_ready` to become 1 while `data` is still 0.

------

## acquire-release: The Golden Pair for Synchronization

This is the most commonly used and most important pair of memory orders to understand.

### release: "Publish" Semantics for Write Operations

`memory_order_release` is used for write operations (store). It guarantees:

- All memory operations before this write will not be reordered after this write
- When other threads read this atomic variable using `acquire`, they can see all modifications made before this write

```cpp
std::atomic<int> flag{0};
int data = 0;

// 线程1：生产者
data = 42;  // 准备数据
flag.store(1, std::memory_order_release);  // 发布：确保data先写入

```

### acquire: "Subscribe" Semantics for Read Operations

`memory_order_acquire` is used for read operations (load). It guarantees:

- All memory operations after this read will not be reordered before this read
- If it reads a value written by another thread's `release`, it can see all modifications that thread made before the release

```cpp
// 线程2：消费者
if (flag.load(std::memory_order_acquire) == 1) {  // 订阅
    // 一定能看到 data == 42
    use(data);
}

```

### Classic Pattern: Publish-Subscribe

```cpp
template<typename T>
class AtomicPtrWithVersion {
public:
    void publish(T* ptr) {
        // 先写数据，再发布指针
        data_ptr.store(ptr, std::memory_order_release);
    }

    T* consume() {
        // 先获取指针，再使用数据
        return data_ptr.load(std::memory_order_acquire);
    }

private:
    std::atomic<T*> data_ptr{nullptr};
};

```

**Embedded scenario**: Passing data between an interrupt and the main thread

```cpp
class UARTBuffer {
public:
    // 主线程：准备数据并通知中断
    void send_byte(uint8_t byte) {
        tx_data = byte;
        ready_to_send.store(true, std::memory_order_release);
        // 触发发送...
    }

    // 中断：发送完成后的处理
    void tx_complete_irq() {
        if (ready_to_send.load(std::memory_order_acquire)) {
            // 一定能看到正确的tx_data
            UART_DR = tx_data;
            ready_to_send.store(false, std::memory_order_release);
        }
    }

private:
    std::atomic<bool> ready_to_send{false};
    uint8_t tx_data;
};

```

### acq_rel: Read-Modify-Write Operations

`memory_order_acq_rel` is used for read-modify-write (RMW) operations, such as `fetch_add`, `compare_exchange`, and so on. It has both acquire and release semantics simultaneously:

```cpp
std::atomic<int> counter{0};

// 既是acquire（读取）又是release（写入）
int old = counter.fetch_add(1, std::memory_order_acq_rel);

```

**Typical application**: Spinlock

```cpp
class SpinLock {
public:
    void lock() {
        // 一直尝试获取锁
        while (!flag.test_and_set(std::memory_order_acquire)) {
            // 可以加个pause指令降低功耗
            #if defined(__x86_64__)
            _mm_pause();
            #elif defined(__ARM_ARCH)
            __yield();
            #endif
        }
    }

    void unlock() {
        flag.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
};

```

------

## seq_cst: Default Sequential Consistency

`memory_order_seq_cst` is the default memory order for all atomic operations and provides the strongest guarantee. On top of acquire-release, it also guarantees:

**All threads see a consistent order of all `seq_cst` operations.**

This sounds simple, but the cost is actually quite high—usually requiring a full memory barrier.

```cpp
std::atomic<int> x{0};
std::atomic<int> y{0};

// 线程1
x.store(1, std::memory_order_seq_cst);

// 线程2
y.store(1, std::memory_order_seq_cst);

// 线程3
int r1 = x.load(std::memory_order_seq_cst);
int r2 = y.load(std::memory_order_seq_cst);

// 线程4
int r3 = y.load(std::memory_order_seq_cst);
int r4 = x.load(std::memory_order_seq_cst);

```

If it were `seq_cst`, then:

- Thread 3 and thread 4 would see a consistent modification order for x and y
- You wouldn't see inconsistencies like "thread 3 sees x change first, while thread 4 sees y change first"

**A perk of the x86 architecture**: x86's TSO (Total Store Ordering) model is already very strong, so `seq_cst` requires almost no extra overhead. But weakly ordered architectures like ARM and PowerPC are a different story.

**When to use seq_cst**:

- When unsure, default to seq_cst first
- In scenarios requiring global ordering guarantees
- When performance isn't the top priority

**When you can skip it**:

- When you clearly only need acquire-release semantics
- On performance-critical paths

------

## consume: The Deprecated Dependency Order

`memory_order_consume` was originally designed to solve the pointer publishing problem: it only guarantees that operations dependent on the load are not reordered, making it lighter than acquire.

```cpp
struct Data {
    int value;
    int array[10];
};

std::atomic<Data*> ptr{nullptr};

// 生产者
Data* d = new Data{42, {1,2,3,4,5,6,7,8,9,10}};
ptr.store(d, std::memory_order_release);

// 消费者
Data* p = ptr.load(std::memory_order_consume);
if (p) {
    // 依赖于p的操作能保证看到正确的数据
    int x = p->value;        // ✅ 有依赖，安全
    int y = p->array[5];     // ✅ 有依赖，安全
}
// 但不依赖p的操作没保证
int global = some_global;   // ❌ 没依赖，可能看到旧值

```

**The problem**: No mainstream compiler truly implements dependency chain tracking; they all treat `consume` as `acquire`.

**C++17 resolution**: Recommends not using `consume`, and it is formally deprecated in C++26. Just use `acquire` instead.

------

## Performance Comparison of Memory Orders

Below is the relative performance of atomic increments using different memory orders across various architectures (rough estimates):

| Architecture | relaxed | acquire/release | seq_cst |
|--------------|---------|----------------|---------|
| x86-64 | 1x | 1x | ~1.2x |
| ARMv8 | 1x | ~2x | ~3x |
| ARMv7 | 1x | ~3x | ~5x |
| PowerPC | 1x | ~4x | ~6x |

**Key takeaways**:

- On x86, acquire/release has almost zero extra overhead (guaranteed by hardware)
- The difference is significant on weakly ordered architectures (ARM/PowerPC)
- `seq_cst` incurs extra costs on any architecture

**Embedded advice**: On cores like Cortex-M that lack out-of-order execution, the overhead of memory orders beyond relaxed is relatively small, but we still need to be careful.

------

## Common Patterns and Pitfalls

### Pattern 1: Flag Synchronization

```cpp
class DataProducer {
public:
    void produce(const std::vector<int>& data) {
        // 1. 准备数据
        this->data = data;

        // 2. 发布：release确保data写入完成
        ready.store(true, std::memory_order_release);
    }

    bool consume(std::vector<int>& out) {
        // 3. 检查：acquire确保能看到完整的data
        if (!ready.load(std::memory_order_acquire)) {
            return false;
        }

        out = data;
        ready.store(false, std::memory_order_release);
        return true;
    }

private:
    std::atomic<bool> ready{false};
    std::vector<int> data;
};

```

### Pattern 2: Circular Buffer Indices

```cpp
class SPSCRingBuffer {
public:
    bool push(int item) {
        const size_t write_pos = write_idx.load(std::memory_order_relaxed);
        const size_t next_pos = (write_pos + 1) % Capacity;

        // acquire：确保读取read_idx是最新值
        if (next_pos == read_idx.load(std::memory_order_acquire)) {
            return false;  // 满
        }

        buffer[write_pos] = item;
        // release：确保数据写入后，再更新write_idx
        write_idx.store(next_pos, std::memory_order_release);
        return true;
    }

    bool pop(int& item) {
        // relaxed：不需要同步
        const size_t read_pos = read_idx.load(std::memory_order_relaxed);

        // acquire：确保读取write_idx是最新值
        if (read_pos == write_idx.load(std::memory_order_acquire)) {
            return false;  // 空
        }

        item = buffer[read_pos];
        const size_t next_pos = (read_pos + 1) % Capacity;
        // release：更新read_idx
        read_idx.store(next_pos, std::memory_order_release);
        return true;
    }

private:
    static constexpr size_t Capacity = 1024;
    std::array<int, Capacity> buffer;
    std::atomic<size_t> read_idx{0};
    std::atomic<size_t> write_idx{0};
};

```

### Pitfall 1: Forgetting acquire/release on One Side

```cpp
std::atomic<bool> flag{0};
int data = 0;

// 线程1
data = 42;
flag.store(true, std::memory_order_release);  // ✅ release

// 线程2
if (flag.load()) {  // ❌ 默认是seq_cst，但问题在于...
    // 这里还是能正确工作，因为seq_cst比acquire更强
}

// 但如果这样：
if (flag.load(std::memory_order_relaxed)) {  // ❌ relaxed！
    use(data);  // data可能不是42！
}

```

**Principle**: Use them in pairs! If one side uses release, the other must use acquire (or something stronger).

### Pitfall 2: Synchronizing Across Multiple Atomic Variables

```cpp
std::atomic<int> a{0};
std::atomic<int> b{0};

// 线程1
a.store(1, std::memory_order_release);
b.store(2, std::memory_order_release);

// 线程2
int y = b.load(std::memory_order_acquire);
int x = a.load(std::memory_order_acquire);
// 你以为 x一定是1？不一定！
// acquire只保证看到b.store之前的修改，但不保证看到a.store

```

**Solutions**:

- Use a single atomic variable for synchronization
- Or use `seq_cst` (but the performance is poor)

### Pitfall 3: Forgetting That Non-Atomic Variables Also Need Synchronization via Memory Order

```cpp
std::atomic<bool> flag{false};
int data = 0;  // ❌ 不是原子的！

// 线程1
data = 42;
flag.store(true, std::memory_order_release);

// 线程2
if (flag.load(std::memory_order_acquire)) {
    // ✅ 能看到正确的data，虽然data不是原子的
    // 因为acquire-release建立了同步关系
}

```

**Key understanding**: Memory order synchronizes memory accesses, not just atomic variables. As long as non-atomic variables are accessed with the correct timing, they can be correctly synchronized too.

------

## Special Considerations for Embedded Systems

### ARM Cortex-M Memory Model

Cortex-M0/M0+: Strictly in-order execution, almost no reordering

Cortex-M3/M4/M7: Has some reordering capabilities, especially the M7

**Recommendations**:

- On M0/M0+: The overhead of memory orders beyond relaxed is very small
- On M3/M4: Use acquire-release moderately
- On M7: You need to seriously consider memory order, as it has store buffers, etc.

### volatile Does Not Equal atomic

```cpp
volatile int flag = 0;  // ❌ 不是线程安全的！

// 线程1
flag = 1;

// 线程2
if (flag) { }

```

`volatile` only guarantees:

- It won't be optimized away by the compiler
- Every access reads from memory

But it **does not guarantee**:

- Atomicity
- Memory ordering

MSVC is an exception; its volatile has acquire-release semantics, but this is non-standard.

### Synchronization Between Interrupts and the Main Thread

```cpp
std::atomic<bool> irq_flag{false};
volatile uint32_t* gpio_reg = reinterpret_cast<uint32_t*>(0x40000000);

// 中断服务程序
void GPIO_IRQHandler() {
    *gpio_reg = (*gpio_reg) | (1 << 5);  // 清除中断
    irq_flag.store(true, std::memory_order_release);
}

// 主线程
void poll_gpio() {
    if (irq_flag.load(std::memory_order_acquire)) {
        // 处理GPIO事件...
        irq_flag.store(false, std::memory_order_release);
    }
}

```

**Note**: We need to be careful when using atomics inside an interrupt service routine (ISR). On some embedded platforms, the atomic library might be implemented using locks, and we cannot block inside an ISR. Be sure to check `is_lock_free()`.

------

## Summary

Memory order is one of the most complex but also most important topics in C++ concurrent programming. Let's recap:

1. **relaxed**: Atomicity only, suitable for counters and statistics
2. **acquire-release**: The most commonly used synchronization pattern, used in pairs
3. **seq_cst**: The default option, strongest guarantee but with a performance cost
4. **consume**: Deprecated, use acquire instead

**Practical advice**:

- Start with the default `seq_cst`, get it working first
- On performance-critical paths, analyze and downgrade to `acquire-release`
- Use `relaxed` for pure counters
- Use release and acquire in pairs
- Read the official documentation and examples more often

**Final reminder**: Correct concurrent programs are hard to write, and memory order is only one part of the puzzle. If you are writing life-critical embedded code, the best advice might be: use locks if you can, use message queues if you can, and leave lock-free algorithms to those who truly need them and truly understand them.

In the next chapter, we will explore more advanced topics in concurrent programming.
