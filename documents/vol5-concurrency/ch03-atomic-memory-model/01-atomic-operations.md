---
title: "atomic 操作"
description: "std::atomic<T> 的完整操作手册：load/store、fetch_add、compare_exchange 与 lock-free 判断"
chapter: 3
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - atomic
difficulty: intermediate
platform: host
reading_time_minutes: 22
cpp_standard: [11, 14, 17, 20]
prerequisites:
  - "latch、barrier 与 semaphore"
related:
  - "内存序详解"
  - "原子操作模式"
---

# atomic 操作

到目前为止，我们讨论的同步原语——mutex、condition variable、latch、barrier、semaphore——本质上都是"先加锁，再操作，最后解锁"的思路。它们很安全，也很直观，但有一个共同的代价：哪怕你只想保护一个简单的整数自增，也得走一遍 lock → modify → unlock 的完整流程。对于"修改一个变量"这种粒度极小的操作来说，这套流程的重量就显得不太匹配了。

`std::atomic` 就是针对这种"极小粒度"场景设计的。它不靠锁（至少在理想情况下），而是直接利用 CPU 提供的原子指令来保证操作不可分割。上一篇中我们在并发基本问题里已经用 `std::atomic<int>` 修复过 data race 了，但当时只是浅尝辄止。这一篇我们要完整地拆解 `std::atomic<T>` 的所有操作——从最基础的 `load`/`store`，到 CAS（compare-and-swap）机制，再到 lock-free 的判断和特化类型 `atomic_flag`。下一篇我们再讨论内存序，这里先把注意力集中在"原子操作能做什么"上。

## std::atomic<T> 支持哪些类型

`std::atomic` 是一个类模板，定义在 `<atomic>` 头文件中。并不是所有类型都能放进 `std::atomic`——标准对此有明确的限制。

对于整型类型——`bool`、`char`、`short`、`int`、`long`、`long long` 以及它们的无符号变体——标准库提供了 `std::atomic` 的显式特化，支持完整的算术和位运算原子操作（`fetch_add`、`fetch_sub`、`fetch_and`、`fetch_or`、`fetch_xor`）。指针类型同样被特化，支持 `fetch_add` 和 `fetch_sub`，用于原子地移动指针。

对于自定义类型 `T`，`std::atomic<T>` 也存在，前提是 `T` 必须满足一个核心条件：`std::is_trivially_copyable<T>::value` 为 true——也就是说 `T` 不能有用户提供的拷贝构造/赋值（`= default` 是可以的）、虚函数、虚基类等。满足这个条件的自定义类型可以使用 `load()`、`store()`、`exchange()`、`compare_exchange_weak/strong()` 这些通用操作，但不能使用 `fetch_add` 等算术操作——标准没有义务为你的自定义类型定义"加法"的语义。

需要注意的是，这些通用操作本身对 `T` 有额外的要求——`load()` 要求 `T` 是 CopyConstructible 的，`store()` 要求 `T` 是 CopyAssignable 的，`exchange()` 和 `compare_exchange_*` 则两者都需要。不过，既然 `T` 是 trivially copyable 的，这些要求几乎总是自动满足的。另外，默认构造函数 `std::atomic<T> a;` 在 C++20 之前会对 `T` 做值初始化（所以需要 `T` 可默认构造），但 C++20 起改为不初始化——如果你用的是 `std::atomic<T> a{T{...}};` 这种带参数的构造，`T` 不需要可默认构造。

```cpp
#include <atomic>
#include <iostream>

// 整型：完全支持
std::atomic<int> atomic_int{0};
std::atomic<unsigned long> atomic_ulong{0};

// 指针：支持 fetch_add/fetch_sub（按元素大小偏移）
struct Node {
    int value;
    Node* next;
};
std::atomic<Node*> atomic_head{nullptr};

// 自定义 trivially-copyable 类型：
// 支持 load/store/exchange/CAS，但不支持 fetch_add
struct alignas(8) PacketHeader {
    uint32_t id;
    uint32_t flags;
};
static_assert(std::is_trivially_copyable_v<PacketHeader>);
std::atomic<PacketHeader> atomic_header{PacketHeader{0, 0}};
```

值得注意的是，C++20 起标准明确支持了 `std::atomic<float>` 和 `std::atomic<double>`，并且为浮点特化提供了 `fetch_add` 和 `fetch_sub`。不过在 C++20 之前，浮点原子变量只能 `load`、`store`、`exchange`、`compare_exchange`，不能直接做原子加减。后面我们会专门讨论浮点原子操作的注意事项。

## load() 与 store()：原子读写的根基

`load()` 和 `store()` 是原子操作中最基础的一对。所有的原子读写最终都归结为这两个操作（加上一个可选的内存序参数）。在不指定内存序的情况下，所有原子操作默认使用 `memory_order_seq_cst`——最强的排序保证。内存序的具体含义我们留到下一篇展开，这里只需要记住：默认参数是安全的，只是不一定是最快的。

```cpp
#include <atomic>
#include <iostream>

int main()
{
    std::atomic<int> value{0};

    // store：原子地写入一个值
    value.store(42);
    value.store(100, std::memory_order_relaxed);

    // load：原子地读取当前值
    int x = value.load();
    int y = value.load(std::memory_order_relaxed);

    // 便捷写法：隐式转换
    int z = value;       // 等价于 value.load()
    value = 200;         // 等价于 value.store(200)

    std::cout << "value = " << value.load() << "\n";
    return 0;
}
```

先别急着用便捷写法。`int z = value;` 看起来像普通的变量拷贝，但它背后是一次原子 load。在一个复杂表达式中混用隐式转换，有时候会让代码的意图变得模糊——到底是普通赋值还是原子读取？团队协作中，笔者更倾向于显式调用 `load()` 和 `store()`，虽然多打几个字符，但一眼就能看出来这是在操作原子变量。

## fetch_add、fetch_sub 与位运算：原子算术

对于整型和指针类型，`std::atomic` 提供了一套 fetch 系列操作。它们执行"读取当前值 → 做运算 → 写回新值"这一整个读-修改-写（Read-Modify-Write, RMW）序列，并且保证这个序列是原子的——中间状态不会被其他线程观察到。

fetch 系列操作的返回值是**修改前的旧值**，而不是新值。这是一个非常务实的设计选择：返回旧值意味着你可以同时完成"读取当前状态"和"修改状态"两件事，这在实现无锁算法时极其方便。

```cpp
#include <atomic>
#include <iostream>

int main()
{
    std::atomic<int> counter{0};

    // fetch_add：原子加法，返回旧值
    int old1 = counter.fetch_add(5);    // counter 变成 5，old1 = 0
    int old2 = counter.fetch_add(3);    // counter 变成 8，old2 = 5

    // fetch_sub：原子减法，返回旧值
    int old3 = counter.fetch_sub(2);    // counter 变成 6，old3 = 8

    // 位运算
    counter.fetch_or(0xFF);    // 按位或
    counter.fetch_and(0xF0);   // 按位与
    counter.fetch_xor(0x0F);   // 按位异或

    std::cout << "counter = " << counter.load() << "\n";
    return 0;
}
```

这些操作也有对应的复合赋值和自增/自减运算符重载，但要注意运算符重载返回的是**新值**（准确地说是应用了运算之后的值），而不是旧值——这和 fetch 系列恰好相反：

```cpp
std::atomic<int> x{10};

// 运算符重载返回新值
int new_val = ++x;       // x 变成 11，new_val = 11
int old_val = x++;       // x 变成 12，old_val = 11（后置返回旧值）
x += 5;                  // x 变成 17
```

笔者在这里强调一个容易混淆的细节：`x++`（后置自增）和 `x.fetch_add(1)` 的效果不完全一样。`x++` 返回的是自增**之前**的值，行为上确实和 `fetch_add(1)` 一致。但 `++x`（前置自增）返回的是自增**之后**的值，它等同于 `x.fetch_add(1) + 1`。在不需要返回值的场景下（比如纯粹的自增计数），用哪个都无所谓；但如果你在表达式中使用了返回值，这个区别就很重要。

## 浮点原子操作的注意事项

这是一个很多人第一次用 `std::atomic<float>` 时会遇到的问题。C++20 起浮点特化确实提供了 `fetch_add` 和 `fetch_sub`，但在使用时需要注意两个层面的特殊性。

硬件层面，绝大多数 CPU 架构没有提供原子浮点加法指令。x86 有 `LOCK XADD` 用于整数原子加法，但浮点加法走的是 FPU/SSE/AVX 执行单元，这些单元本身就不是为原子操作设计的。所以 `atomic<float>::fetch_add` 在大多数平台上内部会退化成 CAS 循环——并没有硬件级的原子浮点加法。

语义层面，浮点加法不是结合律的——`(a + b) + c` 不等于 `a + (b + c)`，因为每次运算都涉及精度舍入。这意味着即使你有多个线程同时对一个浮点原子变量做 `fetch_add`，最终结果依赖于操作的执行顺序，而这个顺序是不确定的。此外，浮点运算的结果可能因浮点环境（舍入模式、精度控制）的不同而变化，这给 `fetch_add` 的语义带来了额外的不可复现性。

如果你需要在 C++20 之前的环境中原子地修改浮点变量，或者需要避免 `fetch_add` 的精度不可复现问题，标准的做法是用 CAS 循环：

```cpp
#include <atomic>

std::atomic<float> atomic_value{0.0f};

float atomic_fetch_add(float delta)
{
    float old_val = atomic_value.load(std::memory_order_relaxed);
    float new_val;
    do {
        new_val = old_val + delta;
        // 如果 atomic_value 还是 old_val，就把它换成 new_val
        // 否则 old_val 被更新为当前值，重试
    } while (!atomic_value.compare_exchange_weak(
                 old_val, new_val, std::memory_order_relaxed));
    return old_val;
}
```

这个模式我们马上会在 CAS 部分再次看到——它是无锁编程的基石。

## compare_exchange_weak 与 compare_exchange_strong：CAS 机制

Compare-And-Swap（CAS）是原子操作里最重要的原语，没有之一。几乎所有无锁数据结构的实现都建立在 CAS 之上。C++ 提供了两个变体：`compare_exchange_weak` 和 `compare_exchange_strong`，它们的区别微妙但关键。

先看接口。两者签名完全一致：

```cpp
bool compare_exchange_weak(T& expected, T desired,
                           std::memory_order success = memory_order_seq_cst,
                           std::memory_order failure = memory_order_seq_cst);

bool compare_exchange_strong(T& expected, T desired,
                             std::memory_order success = memory_order_seq_cst,
                             MemoryOrder failure = memory_order_seq_cst);
```

执行逻辑是这样的：原子地比较当前值和 `expected`。如果相等，就把当前值替换为 `desired` 并返回 `true`；如果不等，就把当前值加载到 `expected` 中并返回 `false`。注意，失败时 `expected` 会被覆盖——这是一个容易被忽略的细节，如果你后续还要用到原始的 `expected` 值，记得提前备份。

两者的区别在于"虚假失败（spurious failure）"：`compare_exchange_weak` 即使在当前值等于 `expected` 的情况下，也可能返回 `false`。这不是 bug，而是硬件层面的限制。在 ARM、PowerPC 这类使用 LL/SC（Load-Linked/Store-Conditional）原语实现 CAS 的架构上，SC 指令可能因为各种原因失败——其他处理器碰了同一个 cache line、中断发生、甚至纯粹的调度事件。x86 使用硬件 `CMPXCHG` 指令，不存在这个问题，所以 x86 上 `weak` 和 `strong` 生成的代码完全相同。

```cpp
#include <atomic>
#include <iostream>

int main()
{
    std::atomic<int> value{10};

    // CAS 成功的场景
    int expected = 10;
    bool ok = value.compare_exchange_strong(expected, 20);
    // ok = true, value = 20, expected 不变

    // CAS 失败的场景
    expected = 10;  // 重新设为 10
    ok = value.compare_exchange_strong(expected, 30);
    // ok = false, value 仍为 20, expected 被更新为 20
    std::cout << "value = " << value.load()
              << ", expected = " << expected << "\n";
    return 0;
}
```

什么时候用 `weak`，什么时候用 `strong`？规则很简单：如果你的 CAS 外面已经包了一个循环，用 `weak`——虚假失败只不过是多转一圈的事，但 `weak` 在 LL/SC 架构上省掉了内部的重试循环，整体更快。如果你是一次性的 CAS（不在循环中），用 `strong`——否则一次虚假失败就可能导致你的逻辑走上错误的分支。

### 用 CAS 实现无锁栈的 push

我们来看一个经典的 CAS 应用场景——无锁栈的 push 操作。这个例子能很好地展示 `compare_exchange_weak` 在循环中的用法：

```cpp
#include <atomic>

struct Node {
    int data;
    Node* next;
};

std::atomic<Node*> head{nullptr};

void push(int value)
{
    Node* new_node = new Node{value, nullptr};

    Node* old_head = head.load(std::memory_order_relaxed);
    do {
        new_node->next = old_head;
        // 尝试把 head 从 old_head 换成 new_node
        // 如果成功，push 完成
        // 如果失败（别人已经改了 head），old_head 被更新为最新值，重试
    } while (!head.compare_exchange_weak(
                 old_head, new_node,
                 std::memory_order_release,
                 std::memory_order_relaxed));
}
```

这段代码的逻辑是：先读取当前的 `head`，把新节点的 `next` 指向它，然后尝试用一次 CAS 把 `head` 换成新节点。如果在我们准备新节点的过程中，另一个线程已经 push 了一个节点（`head` 变了），CAS 就会失败，`old_head` 被更新为最新的 `head`，我们重新设置 `new_node->next` 再试。这个过程一直重复直到 CAS 成功。

你可能注意到 `compare_exchange_weak` 这里接受两个内存序参数：`success` 和 `failure`。成功时使用 `memory_order_release`（因为我们刚写入了一个新节点，需要确保其他线程能看到完整的数据），失败时使用 `memory_order_relaxed`（失败了就不需要任何同步保证，只是重试而已）。

## exchange()：原子交换

`exchange()` 是一个相对简单但很实用的操作：原子地把新值写进去，同时把旧值拿出来。它是 `load` 和 `store` 的组合体，但保证这两步是不可分割的。

```cpp
#include <atomic>
#include <iostream>

int main()
{
    std::atomic<int> flag{0};

    int old = flag.exchange(1);
    // 现在 flag = 1，old = 0
    std::cout << "flag = " << flag.load()
              << ", old = " << old << "\n";
    return 0;
}
```

`exchange()` 的一个典型用途是"状态交接"——原子地把某个状态从 A 切换到 B，同时根据旧状态决定后续行为：

```cpp
#include <atomic>
#include <iostream>

enum class DeviceState { kIdle, kBusy, kError };

std::atomic<DeviceState> state{DeviceState::kIdle};

void try_start_work()
{
    // 原子地尝试从 Idle 切换到 Busy
    DeviceState old = state.exchange(DeviceState::kBusy);
    if (old != DeviceState::kIdle) {
        // 之前不是 Idle，说明有其他线程已经在用了
        // 恢复原状态（或者进入错误处理）
        state.store(old);
        std::cout << "Cannot start: device was " <<
                     static_cast<int>(old) << "\n";
        return;
    }
    // 成功切换到 Busy，开始工作
    std::cout << "Work started\n";
}
```

注意这个例子其实可以用 CAS 写得更精确（`exchange` 会无条件地写入新值，即使旧状态不是 `kIdle`），但 `exchange` 的优势在于简单——如果你只是想把一个值换进去并且知道旧值是什么，`exchange` 比 CAS 循环简洁得多。

## is_lock_free 与 is_always_lock_free

到这里我们一直说"原子操作不靠锁"，但事实并非总是如此。`std::atomic<T>` 是否真的无锁，取决于两个因素：类型 `T` 的大小和目标平台的硬件能力。如果硬件没有对应宽度的原子指令（比如 32 位 ARM 上对 64 位整数的原子操作），编译器就会退而求其次，用内部锁来实现——这时候 `std::atomic` 的操作就不算真正无锁了。

标准库提供了两个接口来查询这一点。`is_lock_free()` 是一个运行时查询，返回 `true` 表示当前对象上的操作是无锁的。`is_always_lock_free` 是一个编译期常量（`static constexpr`），返回 `true` 表示这种类型的原子操作在**所有**该平台上的实例都是无锁的。如果你需要在编译期做静态断言，用 `is_always_lock_free`；如果你需要在运行时做分支判断，用 `is_lock_free()`。

```cpp
#include <atomic>
#include <iostream>

int main()
{
    std::atomic<int> ai;
    std::atomic<long long> all;

    std::cout << "atomic<int>: "
              << (ai.is_lock_free() ? "lock-free" : "uses lock")
              << "\n";
    std::cout << "atomic<long long>: "
              << (all.is_lock_free() ? "lock-free" : "uses lock")
              << "\n";

    // 编译期检查：如果 int 不是 lock-free 的，直接编译报错
    static_assert(std::atomic<int>::is_always_lock_free,
                  "int must be lock-free on this platform!");

    return 0;
}
```

在实际项目中，`is_always_lock_free` 比 `is_lock_free()` 更有价值。原因在于：如果你的代码路径上有分支依赖 `is_lock_free()` 的返回值，那意味着同一份代码在不同运行实例上可能走不同的路径——这在测试和调试中是个噩梦。相比之下，`static_assert` + `is_always_lock_free` 能在编译期就把问题暴露出来：要么这个平台完全支持无锁，要么编译不过，不存在灰色地带。

在嵌入式场景中，这一点尤其重要。32 位 ARM Cortex-M 上，`std::atomic<int>` 几乎总是 lock-free 的（硬件有 `LDREX`/`STREX` 指令对），但 `std::atomic<int64_t>` 在 Cortex-M0/M3 上可能不是。如果你在 ISR 里使用原子操作，务必确认它是 lock-free 的——ISR 里不能阻塞，而锁实现的原子操作会阻塞。

## atomic_flag：标准保证的无锁原语

`std::atomic<T>` 是否 lock-free 取决于平台，但 `std::atomic_flag` 是一个例外——标准保证 `std::atomic_flag` **永远是 lock-free 的**。在所有平台、所有编译器上，无一例外。这使得 `atomic_flag` 成为构建底层同步原语（比如自旋锁）的最可靠基石。

`atomic_flag` 只有两个状态：set（true）和 clear（false）。它提供了三个核心操作：`test_and_set()` 原子地把标志设为 true 并返回之前的值；`clear()` 原子地把标志设为 false；C++20 新增了 `test()` 用于原子地读取当前值而不修改。

```cpp
#include <atomic>
#include <iostream>

int main()
{
    // C++20 起可以直接 {} 初始化
    std::atomic_flag flag{};

    // test_and_set：设置为 true，返回旧值
    bool was_set = flag.test_and_set();
    std::cout << "was_set = " << std::boolalpha << was_set << "\n";

    // test（C++20）：读取当前值
    bool current = flag.test();
    std::cout << "current = " << current << "\n";

    // clear：设置为 false
    flag.clear();
    std::cout << "after clear: " << flag.test() << "\n";

    return 0;
}
```

### 用 atomic_flag 实现自旋锁

`atomic_flag` 最经典的应用就是自旋锁。自旋锁的原理很简单：获取锁的时候不断尝试 `test_and_set`，如果返回 false（之前是 clear 状态），说明成功拿到了锁；如果返回 true（之前已经是 set 状态），说明锁被别人持有，继续转。释放锁的时候调用 `clear`。

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <vector>

class SpinLock {
public:
    void lock()
    {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // 自旋等待：CPU 在空转
            // 在 x86 上可以插入 _mm_pause() 降低功耗
            // 在 ARM 上可以插入 __yield()
        }
    }

    void unlock()
    {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_{};
};

// 使用示例
SpinLock spinlock;
int shared_counter = 0;

void increment(int times)
{
    for (int i = 0; i < times; ++i) {
        spinlock.lock();
        ++shared_counter;
        spinlock.unlock();
    }
}

int main()
{
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(increment, 250000);
    }
    for (auto& t : threads) {
        t.join();
    }
    std::cout << "shared_counter = " << shared_counter << "\n";
    // 输出：shared_counter = 1000000
    return 0;
}
```

自旋锁的缺点很明显：持有锁的时候其他线程在空转，白白浪费 CPU 时间。所以自旋锁只适合临界区非常短的场景——理想情况下，锁持有的时间应该短到"另一个线程还没来得及调度走就已经释放了"。如果临界区比较长，用 `std::mutex`（操作系统级别的阻塞锁）更合适。

C++20 还为 `atomic_flag` 增加了 `wait()` 和 `notify_one()`/`notify_all()` 操作，让自旋锁可以进化成更高效的"等待锁"——获取失败时不再空转，而是把线程挂起，等锁释放时被唤醒。底层在 Linux 上用 `futex`，Windows 上用 `WaitOnAddress`，比纯自旋省 CPU 多了。

## 常见的认知误区

在结束之前，我们快速过几个容易踩的坑。

第一个误区：以为原子变量能解决所有竞态条件。原子操作保证的是**单次访问**的原子性，但它不保证**多次原子操作**之间的原子性。比如：

```cpp
std::atomic<int> x{0};
std::atomic<int> y{0};

// 线程 1
x.store(1);
y.store(2);

// 线程 2
int a = y.load();
int b = x.load();
```

即使 `x` 和 `y` 各自的 `load`/`store` 都是原子的，线程 2 仍然可能看到 `a == 2` 但 `b == 0`——因为两次 `store` 之间、两次 `load` 之间都没有同步关系。这不是原子操作能解决的，需要内存序来约束。下一篇我们会详细展开这个话题。

第二个误区：认为 `volatile` 等价于 `std::atomic`。`volatile` 的语义是"不要优化掉对这个变量的访问"——每次读写都会真正访问内存，不做缓存。但 `volatile` **不保证原子性，也不保证内存序**。`volatile int counter;` 上的 `++counter` 仍然是读-改-写三步操作，仍然会有 data race。`volatile` 的设计初衷是硬件寄存器映射和 signal handler，不是多线程。

第三个误区：在 `std::atomic<std::string>` 这样的非 trivially-copyable 类型上使用 `std::atomic`。标准不允许——编译器会直接报错。`std::string` 有用户定义的拷贝构造函数（内部涉及堆内存分配），不满足 trivially copyable 的要求。如果需要原子地共享字符串，可以用 `std::atomic<std::shared_ptr<std::string>>`（C++20 起支持）或者用 mutex 保护。

## 练习

### 练习 1：无锁计数器

用 `std::atomic<int>` 实现一个多线程安全的计数器。要求启动 8 个线程，每个线程对计数器递增 100000 次，最终结果应该是 800000。分别测试使用 `fetch_add` 和 `compare_exchange_weak` 循环两种实现方式，比较两者的正确性和性能差异。

提示：用 `compare_exchange_weak` 实现 `fetch_add` 的思路是——读取当前值，计算新值，CAS 尝试替换，失败则重试。

### 练习 2：无锁最大值追踪器

实现一个线程安全的最大值追踪器：多个线程不断写入随机值，追踪器始终记录所有写入值中的最大值。要求使用 `compare_exchange_strong`（不是 `fetch_add`）来实现。

提示：`compare_exchange_strong` 的 `expected` 参数在失败时会被更新为当前值——你需要在这个"失败"分支里比较当前值和你的候选新值，决定是否需要重试。

```cpp
class MaxTracker {
public:
    void update(int new_value)
    {
        int current = max_.load(std::memory_order_relaxed);
        while (new_value > current) {
            if (max_.compare_exchange_strong(
                    current, new_value, std::memory_order_relaxed)) {
                break;  // 成功更新
            }
            // 失败：current 被更新为最新值，继续比较
        }
    }

    int get() const
    {
        return max_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<int> max_{std::numeric_limits<int>::min()};
};
```

完成上面的 `update` 函数后，用多线程测试：创建 8 个线程，每个线程随机生成 100000 个值并调用 `update`，最终验证 `get()` 返回的确实是所有线程生成的值中的最大值。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch03-atomic-memory-model/`。

## 参考资源

- [std::atomic -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic)
- [std::atomic_flag -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic_flag)
- [compare_exchange_weak vs compare_exchange_strong -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange)
- [C++ Concurrency in Action, 2nd Edition -- Anthony Williams](https://www.cplusplus.com/reference/atomic/atomic/)
- [atomic is_lock_free -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/is_lock_free)
