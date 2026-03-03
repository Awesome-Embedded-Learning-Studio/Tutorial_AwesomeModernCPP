# 嵌入式现代C++开发——原子操作（std::atomic）

## 引言

你在写多线程代码的时候，有没有遇到过这种莫名其妙的情况：一个变量明明在主线程里改了，工作线程里读到的却还是旧值？或者更糟的，两个线程同时对一个计数器加一，跑了一万次最后计数器却只有九千多？

老实说，笔者当年第一次踩这个坑的时候，对着代码看了整整半天，单步调试一切正常，一跑起来就各种不对劲。后来才明白，这玩意儿叫数据竞争（data race），是并发编程里的头号杀手。

在嵌入式开发中，我们经常要处理中断和多任务的协作。传统的做法是用关中断、自旋锁这些东西，但它们都有开销。C++11给我们带来了一个更轻量的解决方案——`std::atomic`。

> 一句话总结：**原子操作是不可分割的内存访问，要么完全执行，要么完全不执行，不会被其他线程打断。**

------

## 我们为什么需要原子操作

先来看一个有问题的例子：

```cpp
#include <thread>
#include <iostream>

int counter = 0;

void increment() {
    for (int i = 0; i < 10000; ++i) {
        counter++;  // 看似简单，实则危险
    }
}

int main() {
    std::thread t1(increment);
    std::thread t2(increment);
    t1.join();
    t2.join();
    std::cout << "counter = " << counter << '\n';
}
```

你期望输出20000，但实际跑起来可能是12583、16847之类的随机数。问题出在哪里？`counter++`这个操作在CPU层面其实是三步：

1. 从内存读取counter的值
2. 把值加1
3. 把新值写回内存

两个线程同时执行时，可能会出现这种时序：
- 线程1读取counter=100
- 线程2也读取counter=100
- 线程1加1后写回101
- 线程2加1后写回101
- 结果：两次increment只增加了1

这就是数据竞争。用`std::atomic`可以轻松解决：

```cpp
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<int> counter{0};  // 现在是原子变量了

void increment() {
    for (int i = 0; i < 10000; ++i) {
        counter++;  // 原子地自增
    }
}

int main() {
    std::thread t1(increment);
    std::thread t2(increment);
    t1.join();
    t2.join();
    std::cout << "counter = " << counter << '\n';
    // 保证输出：counter = 20000
}
```

------

## std::atomic的基本用法

`std::atomic`是个模板类，可以对整型、指针、甚至自定义类型进行原子封装。但自定义类型有要求：必须可平凡复制（trivially copyable），不能有虚函数之类的。

### 原子变量的定义

```cpp
#include <atomic>

// 基本整型
std::atomic<int> ai;
std::atomic<unsigned long> aul;

// 带初始值
std::atomic<int> counter{0};
std::atomic<bool> flag{false};

// 指针类型
struct Node { int value; };
std::atomic<Node*> node_ptr;

// C++20起支持浮点
std::atomic<double> atomic_double{0.0};
```

### store和load：最基础的读写

原子变量的读写不能直接用`=`和普通取值，得用专门的方法：

```cpp
std::atomic<int> value{0};

// 存储：写入值
value.store(42);                    // 默认内存序
value.store(42, std::memory_order_relaxed);  // 指定内存序

// 加载：读取值
int x = value.load();               // 默认内存序
int y = value.load(std::memory_order_relaxed);

// 更简洁的方式：隐式转换
int z = value;  // 等同于 value.load()
value = 100;    // 等同于 value.store(100)
```

### exchange：交换并返回旧值

`exchange`是个很好用的操作，把新值写进去，同时把旧值拿出来：

```cpp
std::atomic<int> flag{0};

int old = flag.exchange(1);
// 现在 flag = 1，old = 0

// 嵌入式场景：状态机切换
enum class DeviceState { Idle, Busy, Error };
std::atomic<DeviceState> state{DeviceState::Idle};

// 尝试进入Busy状态
DeviceState old_state = state.exchange(DeviceState::Busy);
if (old_state != DeviceState::Idle) {
    // 之前不是Idle，说明有人已经在用了
    // 处理冲突...
}
```

### compare_exchange_weak/strong：CAS操作

这是原子操作里的"核武器"——比较并交换（Compare-And-Swap）。无锁算法的基石。

```cpp
std::atomic<int> value{10};

int expected = 10;
int desired = 20;

// 如果value等于expected，就把value设为desired
// 成功：返回true，value变成20
// 失败：返回false，expected被更新为当前值
bool succeeded = value.compare_exchange_strong(expected, desired);
```

看起来有点绕，但这是实现无锁数据结构的关键：

```cpp
// 无锁栈的push操作
struct Node {
    int data;
    Node* next;
};

std::atomic<Node*> head{nullptr};

void push(int value) {
    Node* new_node = new Node{value, nullptr};

    Node* old_head = head.load();
    do {
        new_node->next = old_head;
        // 如果head还是old_head，就把它设为new_node
        // 如果head已经被别人改了，old_head会被更新为最新值，循环重试
    } while (!head.compare_exchange_weak(old_head, new_node));
}
```

**weak和strong的区别**：
- `compare_exchange_strong`：保证成功就是成功，失败就是失败，但可能因"虚假失败"而多循环几次
- `compare_exchange_weak`：可能"虚假失败"（即使值相等也返回false），但在循环中通常更高效

**嵌入式建议**：在循环中用`weak`，非循环场景用`strong`。

### fetch_add/fetch_sub：原子算术操作

对于整型原子变量，有一系列fetch系列操作：

```cpp
std::atomic<int> counter{0};

// 加法：返回旧值
int old1 = counter.fetch_add(5);   // counter = 5，old1 = 0

// 减法：返回旧值
int old2 = counter.fetch_sub(2);   // counter = 3，old2 = 5

// 位运算
counter.fetch_or(0xFF);    // 按位或
counter.fetch_and(0xF0);   // 按位与
counter.fetch_xor(0x0F);   // 按位异或

// 前置/后置运算符
counter++;      // 等同于 counter.fetch_add(1) + 1
++counter;      // 等同于 counter.fetch_add(1) + 1
counter--;      // 等同于 counter.fetch_sub(1) - 1
--counter;      // 等同于 counter.fetch_sub(1) + 1

counter += 10;  // 等同于 counter.fetch_add(10) + 10
counter -= 5;   // 等同于 counter.fetch_sub(5) - 5
```

C++26还新增了`fetch_max`和`fetch_min`：

```cpp
std::atomic<int> max_value{100};
max_value.fetch_max(150);  // max_value变成150
max_value.fetch_min(80);   // max_value变成80
```

------

## is_lock_free：检查是否真的无锁

这是一个很关键但容易被忽略的点。`std::atomic`的操作在某些平台上可能需要用锁来实现，这就不叫"真正无锁"了。

```cpp
std::atomic<int> ai;
std::atomic<long long> all;

std::cout << "atomic<int> is lock free: "
          << ai.is_lock_free() << '\n';
std::cout << "atomic<long long> is lock free: "
          << all.is_lock_free() << '\n';

// 编译期检查
static_assert(std::atomic<int>::is_always_lock_free,
              "int must be lock-free on this platform!");
```

**为什么有些类型不是lock-free？**
- 某些架构没有对应宽度的原子指令（比如32位ARM上的64位原子操作）
- 编译器决定用锁来实现（但这对程序员透明）

**嵌入式建议**：
- 在性能关键路径上，务必检查`is_lock_free()`
- 如果需要保证无锁，考虑用`std::atomic_ref`（C++20）或者自己实现CAS循环

------

## 嵌入式场景实战

### 场景1：中断与主线程共享标志

```cpp
class UARTRXDriver {
public:
    UARTRXDriver() : data_ready{false}, byte_value{0} {}

    // 中断服务程序（ISR）中调用
    void irq_handler() {
        byte_value = UART_DATA_REG;  // 假设读取硬件寄存器
        data_ready.store(true, std::memory_order_release);
    }

    // 主循环中调用
    bool get_byte(uint8_t& byte) {
        if (data_ready.load(std::memory_order_acquire)) {
            byte = byte_value;
            data_ready.store(false, std::memory_order_release);
            return true;
        }
        return false;
    }

private:
    std::atomic<bool> data_ready;
    std::atomic<uint8_t> byte_value;
};
```

### 场景2：无锁队列的生产者-消费者

```cpp
template<typename T, size_t Size>
class SPSCQueue {
    // 单生产者单消费者，无需锁
public:
    SPSCQueue() : read_idx(0), write_idx(0) {}

    bool push(const T& item) {
        const size_t current_write = write_idx.load(std::memory_order_relaxed);
        const size_t next_write = (current_write + 1) % Size;

        if (next_write == read_idx.load(std::memory_order_acquire)) {
            return false;  // 队列满
        }

        buffer[current_write] = item;
        write_idx.store(next_write, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        const size_t current_read = read_idx.load(std::memory_order_relaxed);

        if (current_read == write_idx.load(std::memory_order_acquire)) {
            return false;  // 队列空
        }

        item = buffer[current_read];
        const size_t next_read = (current_read + 1) % Size;
        read_idx.store(next_read, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Size> buffer;
    std::atomic<size_t> read_idx;
    std::atomic<size_t> write_idx;
};

// 使用
SPSCQueue<int, 1024> uart_rx_queue;

// UART中断中
void uart_irq_handler() {
    uint8_t byte = UART_DR;
    uart_rx_queue.push(byte);
}

// 主线程中
void main_loop() {
    int data;
    while (uart_rx_queue.pop(data)) {
        process_byte(data);
    }
}
```

### 场景3：引用计数（类似shared_ptr）

```cpp
class RefCounted {
public:
    void ref() {
        ref_count.fetch_add(1, std::memory_order_relaxed);
    }

    void unref() {
        // fetch_sub返回旧值，如果减后为0则删除
        if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    int get_count() const {
        return ref_count.load(std::memory_order_relaxed);
    }

protected:
    virtual ~RefCounted() = default;

private:
    std::atomic<int> ref_count{0};
};
```

------

## 常见的坑

### 坑1：误以为原子变量能解决所有竞态

```cpp
std::atomic<int> x{0};
std::atomic<int> y{0};

// 线程1
x.store(1);
y.store(2);

// 线程2
int a = y.load();
int b = x.load();

// 你以为 a=2 一定意味着 b=1？
// 错！没有合适的内存序保证，可能是 a=2, b=0
```

这个问题我们下一章讲内存序的时候会详细展开。

### 坑2：忘记考虑操作的原子性范围

```cpp
std::atomic<int> array[10];  // 10个原子int

// 这不是原子的！
array[0].store(array[1].load());  // 两次独立的原子操作
```

如果你需要"同时"操作多个原子变量，还是得用锁或者其他同步机制。

### 坑3：对非平凡类型使用atomic

```cpp
// ❌ 编译错误：string不是平凡可复制类型
std::atomic<std::string> str;

// ✅ 用指针或者shared_ptr
std::atomic<std::shared_ptr<std::string>> str_ptr;
// 或者C++20的atomic_ref配合外部锁
```

### 坑4：在嵌入式平台上忽视对齐要求

某些平台的原子操作要求对齐，比如ARMv6上：

```cpp
// 可能不对齐，导致总线错误
char buffer[sizeof(std::atomic<int>) * 2];
auto p = new (&buffer[1]) std::atomic<int>;  // 危险！

// ✅ 使用alignas
alignas(std::atomic<int>) char buffer[sizeof(std::atomic<int>) * 2];
```

------

## C++20新特性：atomic_ref和atomic_wait

### atomic_ref：对现有变量进行原子操作

以前如果你有一个普通变量想原子操作，得创建一个atomic包装它。C++20提供了`atomic_ref`，可以对任何现有变量进行原子访问：

```cpp
int regular_int = 0;

std::atomic_ref<int> atomic_ref_int(regular_int);

atomic_ref_int.store(42);
int x = atomic_ref_int.load();

// 注意：atomic_ref的生命周期不能超过原变量！
```

**嵌入式场景**：硬件寄存器映射

```cpp
// 假设这是硬件映射的内存地址
volatile uint32_t* hw_reg = reinterpret_cast<uint32_t*>(0x40000000);

// 可以用atomic_ref来原子访问
std::atomic_ref<uint32_t> atomic_reg(*const_cast<uint32_t*>(hw_reg));

atomic_reg.fetch_or(0x01);
```

### atomic_wait/notify：高效的等待机制

C++20引入了类似条件变量但更高效的等待/通知机制：

```cpp
std::atomic<int> signal{0};

// 等待线程
void waiter() {
    int expected = 0;
    // 等待signal变成非0，比自旋等待更省CPU
    signal.wait(expected);
    // 醒来后处理...
}

// 通知线程
void notifier() {
    signal.store(1);
    signal.notify_one();  // 唤醒一个等待者
    // signal.notify_all();  // 或者唤醒全部
}
```

**嵌入式场景**：高效的任务同步

```cpp
class TaskSignal {
public:
    void wait() {
        int expected = 0;
        // 这里的实现可能用WFE指令（ARM）之类的低功耗等待
        flag.wait(expected);
    }

    void signal() {
        flag.store(1, std::memory_order_release);
        flag.notify_one();
    }

    void reset() {
        flag.store(0, std::memory_order_release);
    }

private:
    std::atomic<int> flag{0};
};
```

------

## 性能考虑

原子操作的开销主要来自三个方面：

1. **禁止编译器优化**：编译器不能把原子操作优化掉或者重排
2. **CPU指令开销**：某些原子操作需要特殊的指令（如x86的lock前缀）
3. **缓存一致性流量**：多核之间同步缓存行

因为这个，我们可能就会有如下的策略
- 能用`relaxed`就别用更强的内存序（下一章细讲）
- 能用局部变量就别用共享的原子变量
- 考虑用线程局部存储（`thread_local`）减少原子操作频率
- 对于高频计数器，考虑用per-thread计数然后定期汇总

```cpp
// 高频计数优化示例
thread_local int local_counter = 0;
std::atomic<int> global_counter{0};

void increment() {
    local_counter++;  // 无原子开销
    if (local_counter >= 1000) {
        global_counter.fetch_add(local_counter, std::memory_order_relaxed);
        local_counter = 0;
    }
}
```

------

## 小结

`std::atomic`是现代C++并发编程的基石，掌握它意义重大：

1. **基本操作**：`store`、`load`、`exchange`、`compare_exchange`、`fetch_*`
2. **检查无锁保证**：`is_lock_free()`告诉你是否真正无锁
3. **嵌入式应用**：中断标志、无锁队列、引用计数
4. **C++20新特性**：`atomic_ref`、`atomic_wait`让原子操作更灵活

但这里有个关键问题我们还没深入——**内存序（Memory Order）**。为什么有时候`relaxed`就够了，什么时候必须用`acquire-release`？这正是下一章要讲的内容，也是很多并发bug的藏身之处。