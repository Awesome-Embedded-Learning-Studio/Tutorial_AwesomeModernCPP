# 嵌入式现代C++开发——内存序（Memory Order）

## 引言

上一章我们讲了`std::atomic`的基本用法，但你有没有注意到每个函数后面那个可选的`std::memory_order`参数？很多人（包括当年的笔者）都是直接用默认值，从来不去管它。

这事儿在大多数情况下能跑，但一遇到稍微复杂点的并发场景，各种灵异现象就出来了：明明先写的数据，对方线程就是看不到；或者两个线程看到的操作顺序完全不一样。

问题的根源在于：**编译器和CPU都会"自作主张"地重排你的指令**。你以为的顺序，和实际执行的顺序，可能压根就不是一回事。

C++内存模型就是用来管理这件事的。它定义了六种内存序，让你在"性能"和"可预测性"之间做选择。

> 一句话总结：**内存序控制编译器和CPU如何重排内存操作，决定了多线程之间能看到什么样的执行顺序。**

------

## 先搞清楚：为什么要重排

在深入内存序之前，我们得先理解为什么编译器和CPU要重排指令。

### 编译器重排

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

你期待线程2一定能看到`a==1`，但编译器可能会觉得：`a`和`b`又不相关，谁先谁后无所谓。于是它排成了：

```cpp
b = 2;  // 先写b
a = 1;  // 后写a
```

这下线程2可能看到`b==2`但`a==0`了。

### CPU重排

更麻烦的是CPU层面的乱序执行。现代CPU都是超标量、流水线的，为了榨干性能，指令不一定按程序顺序执行。

举个例子，ARM Cortex-M7这种有乱序执行的核心，可能会这样：

```asm
STR r0, [a]    ; 写a
STR r1, [b]    ; 写b
; CPU可能实际执行顺序相反
```

**为什么要这么做？** 简单说就是为了快：
- 避免流水线停顿
- 利用缓存预取
- 减少数据依赖等待

但代价就是：多线程编程变得极其复杂。

------

## 六种内存序一览

C++定义了六种内存序，按从弱到强排列：

| 内存序 | 枚举值 | 语义 | 典型应用 |
|--------|--------|------|----------|
| 宽松 | `relaxed` | 只保证原子性，不保证顺序 | 计数器、统计 |
| 消费 | `consume` | 保证依赖链顺序 | 指针发布（已弃用） |
| 获取 | `acquire` | 读操作，禁止后续重排 | 读锁、读取共享数据 |
| 释放 | `release` | 写操作，禁止前序重排 | 写锁、发布共享数据 |
| 获取-释放 | `acq_rel` | 读-改-写操作 | RMW操作 |
| 序列一致 | `seq_cst` | 默认，最强保证 | 需要全局顺序的场景 |

接下来我们逐个深入理解。

------

## relaxed：只保原子性

`memory_order_relaxed`是最轻量的内存序，它只保证操作本身是原子的，其他什么都不管。

```cpp
std::atomic<int> counter{0};

// 多个线程自增
counter.fetch_add(1, std::memory_order_relaxed);
```

**特点**：
- 不保证不同原子变量之间的顺序
- 不保证跨线程的可见性顺序
- 性能最好

**适用场景**：计数器

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

**危险示例**：用relaxed做同步是错误的

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

这里的问题在于：`data_ready`的写和`data`的写可能被CPU重排，导致`data_ready`变成1了，但`data`还是0。

------

## acquire-release：同步的黄金搭档

这是最常用也最需要理解的一对内存序。

### release：写操作时的"发布"语义

`memory_order_release`用于写操作（store），它保证：
- 这个写入之前的所有内存操作不会被重排到这个写入之后
- 其他线程用`acquire`读取这个原子变量时，能看到这个写入之前的所有修改

```cpp
std::atomic<int> flag{0};
int data = 0;

// 线程1：生产者
data = 42;  // 准备数据
flag.store(1, std::memory_order_release);  // 发布：确保data先写入
```

### acquire：读操作时的"订阅"语义

`memory_order_acquire`用于读操作（load），它保证：
- 这个读取之后的所有内存操作不会被重排到这个读取之前
- 如果读到了某个线程`release`写入的值，就能看到那个线程在release之前的所有修改

```cpp
// 线程2：消费者
if (flag.load(std::memory_order_acquire) == 1) {  // 订阅
    // 一定能看到 data == 42
    use(data);
}
```

### 经典模式：发布-订阅

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

**嵌入式场景**：中断与主线程的数据传递

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

### acq_rel：读-改-写操作

`memory_order_acq_rel`用于读-改-写（RMW）操作，比如`fetch_add`、`compare_exchange`等。它同时具有acquire和release的语义：

```cpp
std::atomic<int> counter{0};

// 既是acquire（读取）又是release（写入）
int old = counter.fetch_add(1, std::memory_order_acq_rel);
```

**典型应用**：自旋锁

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

## seq_cst：默认的序列一致性

`memory_order_seq_cst`是所有原子操作的默认内存序，也是最强的保证。它在acquire-release的基础上还保证：

**所有线程看到所有`seq_cst`操作的顺序是一致的**。

这听起来很简单，但实际上代价很大——通常需要完整的内存屏障。

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

如果是`seq_cst`，那么：
- 线程3和线程4看到的x和y的修改顺序是一致的
- 不会出现"线程3看到x先变，线程4看到y先变"这种不一致

**x86架构的福利**：x86的TSO（Total Store Ordering）模型本身就很强，`seq_cst`几乎不需要额外开销。但ARM、PowerPC这些弱序架构就不一样了。

**什么时候用seq_cst**：
- 不确定的时候，先默认用seq_cst
- 需要全局顺序保证的场景
- 性能不是第一优先级时

**什么时候可以不用**：
- 明确只需要acquire-release语义
- 在性能关键路径上

------

## consume：已被弃用的依赖序

`memory_order_consume`原本是为了解决指针发布问题：只保证依赖于该加载的操作不会被重排，比acquire更轻量。

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

**问题**：没有主流编译器真正实现了依赖链跟踪，它们都把`consume`当`acquire`处理。

**C++17的决议**：建议不要用`consume`，C++26正式弃用。直接用`acquire`就好。

------

## 各种内存序的性能对比

下面是在不同架构上，用不同内存序做原子自增的相对性能（粗略估算）：

| 架构 | relaxed | acquire/release | seq_cst |
|------|---------|----------------|---------|
| x86-64 | 1x | 1x | ~1.2x |
| ARMv8 | 1x | ~2x | ~3x |
| ARMv7 | 1x | ~3x | ~5x |
| PowerPC | 1x | ~4x | ~6x |

**关键点**：
- x86架构的acquire/release几乎零额外开销（硬件保证了）
- 弱序架构（ARM/PowerPC）上差异明显
- `seq_cst`在任何架构上都有额外成本

**嵌入式建议**：Cortex-M这种没有乱序执行的核，relaxed之外的内存序开销相对较小，但仍需谨慎。

------

## 常见模式与陷阱

### 模式1：标志位同步

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

### 模式2：循环缓冲区的索引

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

### 陷阱1：漏掉一边的acquire/release

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

**原则**：配对使用！一边release，另一边必须acquire（或更强的）。

### 陷阱2：跨多个原子变量的同步

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

**解决方法**：
- 用单个原子变量做同步
- 或者用`seq_cst`（但性能差）

### 陷阱3：忘记非原子变量也需要通过内存序同步

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

**关键理解**：内存序同步的是内存访问，不只是原子变量。非原子变量只要访问时序正确，也能被正确同步。

------

## 嵌入式特殊考量

### ARM Cortex-M的内存模型

Cortex-M0/M0+：严格按序执行，几乎没有重排

Cortex-M3/M4/M7：有部分重排能力，特别是M7

**建议**：
- M0/M0+上：relaxed之外的内存序开销很小
- M3/M4：适度使用acquire-release
- M7：需要认真考虑内存序，有store buffer等

### volatile不等于atomic

```cpp
volatile int flag = 0;  // ❌ 不是线程安全的！

// 线程1
flag = 1;

// 线程2
if (flag) { }
```

`volatile`只保证：
- 不被编译器优化掉
- 每次访问都从内存读取

但它**不保证**：
- 原子性
- 内存序

MSVC是个例外，它的volatile有acquire-release语义，但这是非标准的。

### 中断与主线程的同步

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

**注意**：中断服务程序里用atomic要小心，有些嵌入式平台的atomic库可能用锁实现，而ISR里不能阻塞。务必检查`is_lock_free()`。

------

## 小结

内存序是C++并发编程里最复杂但也最重要的内容之一。让我们回顾一下：

1. **relaxed**：只保原子性，适用于计数器、统计
2. **acquire-release**：最常用的同步模式，配对使用
3. **seq_cst**：默认选项，最强保证但有性能代价
4. **consume**：已被弃用，用acquire代替

**实践建议**：
- 先用默认的`seq_cst`，能跑再说
- 性能关键路径上，分析后降级到`acquire-release`
- 纯计数器用`relaxed`
- 配对使用release和acquire
- 多读官方文档和示例

**最后提醒**：正确的并发程序很难写，内存序只是其中一部分。如果你在写生命攸关的嵌入式代码，最好的建议可能是：能用锁就用锁，能用消息队列就用消息队列，无锁算法留给那些真有需求且真懂的人。

下一章，我们将探讨更多并发编程的高级话题。
